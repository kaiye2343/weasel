using System.IO.Compression;
using System.Text;
using System.Text.RegularExpressions;
using YamlDotNet.RepresentationModel;

namespace Shorekeeper.Core;

public sealed class InputSettings
{
    public bool Simplified { get; set; } = true;
    public string PinyinMode { get; set; } = "全拼";
    public bool EnableCorrection { get; set; } = true;
    public bool ContextReorder { get; set; } = true;
    public List<string> FuzzyGroups { get; set; } = [];
    public Dictionary<string, string> Shortcuts { get; set; } = [];
}
public sealed class RimeProfiles
{
    public string Root { get; }
    public RimeProfiles(string root) { Root = Path.GetFullPath(root); }
    public static void ValidateId(string id) { if (!Regex.IsMatch(id, "^[a-z][a-z0-9_]{0,31}$")) throw new InvalidDataException("场景标识格式无效。"); }
    static readonly Dictionary<string, string[]> Fuzzy = new()
    {
        ["z_zh"] = ["derive/^zh/z/", "derive/^z(?!h)/zh/"],
        ["c_ch"] = ["derive/^ch/c/", "derive/^c(?!h)/ch/"],
        ["s_sh"] = ["derive/^sh/s/", "derive/^s(?!h)/sh/"],
        ["an_ang"] = ["derive/ang([7890]?)$/an$1/", "derive/an([7890]?)$/ang$1/"],
        ["en_eng"] = ["derive/eng([7890]?)$/en$1/", "derive/en([7890]?)$/eng$1/"],
        ["in_ing"] = ["derive/ing([7890]?)$/in$1/", "derive/in([7890]?)$/ing$1/"],
        ["n_l"] = ["derive/^n/l/", "derive/^l/n/"],
        ["f_h"] = ["derive/^f/h/", "derive/^h/f/"]
    };
    public static IReadOnlyCollection<string> FuzzyNames => Fuzzy.Keys;
    static YamlMappingNode Read(string path)
    {
        if (!File.Exists(path)) return new();
        var yaml = new YamlStream(); using var reader = File.OpenText(path); yaml.Load(reader);
        return yaml.Documents[0].RootNode as YamlMappingNode ?? throw new InvalidDataException("YAML根节点不是映射。");
    }
    static YamlMappingNode Map(YamlMappingNode parent, string key)
    {
        var scalar = new YamlScalarNode(key);
        if (parent.Children.TryGetValue(scalar, out var node)) return node as YamlMappingNode ?? throw new InvalidDataException("补丁结构不兼容。");
        var result = new YamlMappingNode(); parent.Add(scalar, result); return result;
    }
    static string? Scalar(YamlMappingNode map, string key) => map.Children.TryGetValue(new YamlScalarNode(key), out var n) ? (n as YamlScalarNode)?.Value : null;
    static string Serialize(YamlMappingNode map) { var writer = new StringWriter(); new YamlStream(new YamlDocument(map)).Save(writer, false); return writer.ToString(); }
    public string WriteBackedUp(string path, string content)
        => WriteBatchBackedUp(new Dictionary<string, string> { [path] = content });
    public string WriteBatchBackedUp(IReadOnlyDictionary<string, string> changes)
    {
        var files = changes.Select(p => (Path: Path.GetFullPath(p.Key), Text: p.Value)).ToList();
        if (files.Any(f => !f.Path.StartsWith(Root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) ||
            files.Select(f => f.Path).Distinct(StringComparer.OrdinalIgnoreCase).Count() != files.Count)
            throw new InvalidDataException("配置路径越界或重复。");
        Directory.CreateDirectory(Root);
        string backup = Path.Combine(Root, "backups", "shorekeeper", DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fffffff") + "-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(backup);
        var staged = new List<(string Path, string Temp, string? Backup)>();
        var committed = new List<(string Path, string? Backup)>();
        try
        {
            foreach (var file in files)
            {
                Directory.CreateDirectory(Path.GetDirectoryName(file.Path)!);
                string? previous = File.Exists(file.Path) ? Path.Combine(backup, Path.GetRelativePath(Root, file.Path)) : null;
                if (previous != null) { Directory.CreateDirectory(Path.GetDirectoryName(previous)!); File.Copy(file.Path, previous); }
                string temp = file.Path + "." + Guid.NewGuid().ToString("N") + ".tmp";
                staged.Add((file.Path, temp, previous)); File.WriteAllText(temp, file.Text, new UTF8Encoding(false));
            }
            SkinJson.WriteAtomic(Path.Combine(backup, "restore-index.json"), staged.Select(f => new { RelativePath = Path.GetRelativePath(Root, f.Path), Existed = f.Backup != null }));
            foreach (var file in staged) { File.Move(file.Temp, file.Path, true); committed.Add((file.Path, file.Backup)); }
        }
        catch
        {
            foreach (var file in committed.AsEnumerable().Reverse())
                if (file.Backup != null) File.Copy(file.Backup, file.Path, true); else File.Delete(file.Path);
            throw;
        }
        finally { foreach (var file in staged) if (File.Exists(file.Temp)) File.Delete(file.Temp); }
        return backup;
    }
    string Schema(string id) { ValidateId(id); return Path.Combine(Root, $"wanxiang_{id}.schema.yaml"); }
    string Custom(string id) { ValidateId(id); return Path.Combine(Root, $"wanxiang_{id}.custom.yaml"); }
    static YamlSequenceNode Switches(YamlMappingNode schema) => schema.Children.TryGetValue("switches", out var node) && node is YamlSequenceNode sequence ? sequence : throw new InvalidDataException("缺少方案开关。");
    static int FindSwitch(YamlSequenceNode switches, string name)
    {
        for (int i = 0; i < switches.Children.Count; i++) if (switches.Children[i] is YamlMappingNode item &&
            (Scalar(item, "name") == name || item.Children.TryGetValue("options", out var options) && options is YamlSequenceNode list && list.Children.OfType<YamlScalarNode>().Any(n => n.Value == name))) return i;
        throw new InvalidDataException("当前万象版本缺少开关：" + name);
    }
    public InputSettings Load(string id)
    {
        var patch = Map(Read(Custom(id)), "patch"); var schema = Read(Schema(id));
        var result = new InputSettings { EnableCorrection = Scalar(patch, "translator/enable_correction") != "false" };
        var switches = Switches(schema);
        result.Simplified = Scalar(patch, $"switches/@{FindSwitch(switches, "s2s")}/reset") is not ("1" or "2" or "3");
        result.ContextReorder = Scalar(patch, $"switches/@{FindSwitch(switches, "context_reorder")}/reset") != "0";
        if (patch.Children.TryGetValue("speller/algebra/__patch", out var mode) && mode is YamlSequenceNode modes && modes.Children.FirstOrDefault() is YamlScalarNode first)
            result.PinyinMode = first.Value?.Split('/').Last() ?? "全拼";
        if (patch.Children.TryGetValue("speller/algebra/+", out var fuzzy) && fuzzy is YamlSequenceNode rules)
        {
            var values = rules.Children.OfType<YamlScalarNode>().Select(n => n.Value).ToHashSet();
            result.FuzzyGroups = Fuzzy.Where(pair => pair.Value.Any(values.Contains)).Select(pair => pair.Key).ToList();
        }
        string phrases = Path.Combine(Root, $"shortcuts_{id}.dict.yaml");
        if (File.Exists(phrases)) foreach (var line in File.ReadLines(phrases).SkipWhile(l => l.Trim() != "...").Skip(1)) { var parts = line.Split('\t'); if (parts.Length == 3) result.Shortcuts[parts[1]] = parts[0]; }
        return result;
    }
    public string Apply(string id, InputSettings settings)
    {
        var modes = new[] { "全拼", "自然码", "小鹤双拼", "微软双拼", "搜狗双拼", "智能ABC", "紫光双拼", "拼音加加" };
        if (!modes.Contains(settings.PinyinMode) || settings.FuzzyGroups.Any(n => !Fuzzy.ContainsKey(n))) throw new InvalidDataException("不支持的拼音模式或模糊音组。");
        if (settings.Shortcuts.Any(p => !Regex.IsMatch(p.Key, "^[a-z][a-z0-9]{0,31}$") || p.Value.Length is < 1 or > 512 || p.Value.Contains('\n') || p.Value.Contains('\t')))
            throw new InvalidDataException("快捷短语格式无效。");
        string file = Custom(id); var document = Read(file); var patch = Map(document, "patch"); var switches = Switches(Read(Schema(id)));
        patch.Children[new YamlScalarNode($"switches/@{FindSwitch(switches, "s2s")}/reset")] = new YamlScalarNode(settings.Simplified ? "0" : "1");
        patch.Children[new YamlScalarNode($"switches/@{FindSwitch(switches, "context_reorder")}/reset")] = new YamlScalarNode(settings.ContextReorder ? "1" : "0");
        patch.Children[new YamlScalarNode("translator/enable_correction")] = new YamlScalarNode(settings.EnableCorrection ? "true" : "false");
        // Preserve rules not owned by this module, including earlier fuzzy spellings.
        var known = Fuzzy.Values.SelectMany(v => v).Concat(new[] { "derive/^z/zh/", "derive/^c/ch/", "derive/^s/sh/" }).ToHashSet();
        var rules = patch.Children.TryGetValue("speller/algebra/+", out var old) && old is YamlSequenceNode oldRules
            ? oldRules.Children.OfType<YamlScalarNode>().Where(n => !known.Contains(n.Value ?? "")).Select(n => new YamlScalarNode(n.Value)).ToList() : [];
        rules.AddRange(settings.FuzzyGroups.Distinct().SelectMany(n => Fuzzy[n]).Select(s => new YamlScalarNode(s)));
        patch.Children[new YamlScalarNode("speller/algebra/+")] = new YamlSequenceNode(rules);
        patch.Children[new YamlScalarNode("speller/algebra/__patch")] = new YamlSequenceNode(new YamlScalarNode("wanxiang_algebra:/base/" + settings.PinyinMode));
        patch.Children[new YamlScalarNode("translator/prism")] = new YamlScalarNode("wanxiang_" + id);
        patch.Children[new YamlScalarNode("custom_phrase/dictionary")] = new YamlScalarNode("shortcuts_" + id);
        patch.Children[new YamlScalarNode("custom_phrase/prism")] = new YamlScalarNode("shortcuts_" + id);
        return WriteBatchBackedUp(new Dictionary<string, string>
        {
            [file] = Serialize(document),
            [Path.Combine(Root, $"shortcuts_{id}.dict.yaml")] = ShortcutDictionary(id, settings.Shortcuts)
        });
    }
    static string ShortcutDictionary(string id, Dictionary<string, string> phrases) =>
        $"---\nname: shortcuts_{id}\nversion: '1.0'\nsort: by_weight\nuse_preset_vocabulary: false\nimport_tables: [custom_phrase]\n...\n" +
        string.Join('\n', phrases.Select(p => $"{p.Value}\t{p.Key}\t100")) + "\n";
    public void Create(string id)
    {
        string target = Schema(id); if (File.Exists(target) || File.Exists(Custom(id)) || File.Exists(Path.Combine(Root, id + ".dict.yaml"))) throw new IOException("场景或同名词库已存在。");
        var doc = Read(Path.Combine(Root, "wanxiang.schema.yaml"));
        if (doc.Children.Count == 0) throw new InvalidDataException("请先安装万象Base。");
        var schema = Map(doc, "schema"); schema.Children[new YamlScalarNode("schema_id")] = new YamlScalarNode("wanxiang_" + id);
        schema.Children[new YamlScalarNode("name")] = new YamlScalarNode("万象拼音·" + id);
        if (!schema.Children.TryGetValue("dependencies", out var deps) || deps is not YamlSequenceNode dependencies) throw new InvalidDataException("当前万象版本缺少依赖方案列表。");
        dependencies.Add(new YamlScalarNode("shortcuts_" + id));
        var custom = new YamlMappingNode(); var patch = Map(custom, "patch");
        foreach (var (key, value) in new[] { ("translator/user_dict", "wanxiang_" + id), ("wanxiang_english/user_dict", "en_" + id),
            ("user_dict_set/user_dict", "zc_" + id), ("add_user_dict/user_dict", "zc_" + id), ("context_reorder/db_name", "context_reorder_" + id),
            ("super_sequence/db_name", "sequence_" + id), ("input_stats/db_name", "stats_" + id) }) patch.Add(key, value);
        patch.Add("translator/enable_user_dict", "true");
        patch.Add("translator/enable_sentence", "true");
        patch.Add("translator/prism", "wanxiang_" + id);
        patch.Add("add_user_dict/enable_user_dict", "true");
        patch.Add("add_user_dict/enable_auto_phrase", "true");
        patch.Add("custom_phrase/dictionary", "shortcuts_" + id);
        patch.Add("custom_phrase/prism", "shortcuts_" + id);
        patch.Add($"switches/@{FindSwitch(Switches(doc), "s2s")}/reset", "0");
        patch.Add($"switches/@{FindSwitch(Switches(doc), "context_reorder")}/reset", "1");
        patch.Add("translator/packs", new YamlSequenceNode(new YamlScalarNode(id)));
        var defaults = Read(Path.Combine(Root, "default.custom.yaml")); var defaultPatch = Map(defaults, "patch");
        if (!defaultPatch.Children.TryGetValue("schema_list", out var node) || node is not YamlSequenceNode list) throw new InvalidDataException("default.custom.yaml缺少显式方案列表；请先在原配置中设置列表。");
        list.Add(new YamlMappingNode { { "schema", "wanxiang_" + id } });
        WriteBatchBackedUp(new Dictionary<string, string>
        {
            [target] = Serialize(doc), [Custom(id)] = Serialize(custom),
            [Path.Combine(Root, id + ".dict.yaml")] = $"---\nname: {id}\nversion: '1.0'\nsort: by_weight\n...\n",
            [Path.Combine(Root, $"shortcuts_{id}.dict.yaml")] = ShortcutDictionary(id, []),
            [Path.Combine(Root, $"shortcuts_{id}.schema.yaml")] = $"schema:\n  schema_id: shortcuts_{id}\n  name: '场景短语·{id}'\n  version: '1.0'\nspeller:\n  alphabet: abcdefghijklmnopqrstuvwxyz0123456789\n  delimiter: \" '\"\ntranslator:\n  dictionary: shortcuts_{id}\n  prism: shortcuts_{id}\n  enable_user_dict: false\nengine:\n  processors: [speller, selector, express_editor]\n  segmentors: [abc_segmentor]\n  translators: [table_translator]\n",
            [Path.Combine(Root, "default.custom.yaml")] = Serialize(defaults)
        });
    }
    public void ImportDictionary(string id, string source)
    {
        ValidateId(id); if (new FileInfo(source).Length > 20 * 1024 * 1024) throw new InvalidDataException("词库超过20MiB。");
        var rows = new List<string>();
        foreach (string line in File.ReadLines(source))
        {
            if (string.IsNullOrWhiteSpace(line) || line.StartsWith('#')) continue;
            var fields = line.Split('\t');
            if (fields.Length is < 2 or > 3 || !Regex.IsMatch(fields[1], "^[a-züv ']+$") || fields[0].Length > 256 || (fields.Length == 3 && !int.TryParse(fields[2], out _))) throw new InvalidDataException("词库须为：词语<TAB>拼音<TAB>可选词频。");
            rows.Add(line);
        }
        string path = Path.Combine(Root, id + ".dict.yaml");
        if (!File.Exists(path)) throw new InvalidDataException("场景词库不存在。");
        var lines = File.ReadAllLines(path).ToList(); int marker = lines.FindIndex(s => s.Trim() == "...");
        if (marker < 0) throw new InvalidDataException("原词库头部无效。");
        var merged = lines.Skip(marker + 1).Concat(rows).Where(s => !string.IsNullOrWhiteSpace(s)).Distinct();
        WriteBackedUp(path, string.Join('\n', lines.Take(marker + 1).Concat(merged)) + "\n");
    }
    public void ExportAndroid(string destination)
    {
        if (File.Exists(destination)) throw new IOException("导出目标已存在。");
        using var zip = ZipFile.Open(destination, ZipArchiveMode.Create);
        foreach (string file in Directory.EnumerateFiles(Root, "*", SearchOption.AllDirectories))
        {
            string rel = Path.GetRelativePath(Root, file).Replace('\\', '/');
            if (rel.Split('/').Any(p => p is "build" or "backups" or "sync" || p.EndsWith(".userdb")) ||
                Path.GetFileName(file) is "installation.yaml" or "user.yaml" || Path.GetFileName(file).StartsWith("weasel") ||
                Path.GetExtension(file) is not (".yaml" or ".lua" or ".json" or ".txt" or ".ocd2")) continue;
            zip.CreateEntryFromFile(file, "rime/" + rel);
        }
        using var writer = new StreamWriter(zip.CreateEntry("安装说明.txt").Open());
        writer.Write("从Trime官方发布安装同文输入法，在其用户目录导入rime目录内容；另行复制同版本wanxiang-lts-zh-hans.gram模型后部署。不要复制Windows installation.yaml，手机须生成自己的installation_id。使用Ctrl+`或方案菜单切换工作/日常；学习数据通过官方同步快照合并。\n本包不含语法模型及学习数据库。手机实机兼容与WebDAV合并仍需验证。\n");
    }
}
