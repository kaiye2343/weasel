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
    public int? AnnotationLength { get; set; }
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
        => WriteChangesBackedUp(changes.ToDictionary(c => c.Key, c => (string?)c.Value));
    public string WriteChangesBackedUp(IReadOnlyDictionary<string, string?> changes)
    {
        var files = changes.Select(p => (Path: Path.GetFullPath(p.Key), Text: p.Value)).ToList();
        if (files.Any(f => !f.Path.StartsWith(Root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) ||
            files.Select(f => f.Path).Distinct(StringComparer.OrdinalIgnoreCase).Count() != files.Count)
            throw new InvalidDataException("配置路径越界或重复。");
        Directory.CreateDirectory(Root);
        string backup = Path.Combine(Root, "backups", "shorekeeper", DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fffffff") + "-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(backup);
        var staged = new List<(string Path, string? Temp, string? Backup)>();
        var committed = new List<(string Path, string? Backup)>();
        try
        {
            foreach (var file in files)
            {
                Directory.CreateDirectory(Path.GetDirectoryName(file.Path)!);
                string? previous = File.Exists(file.Path) ? Path.Combine(backup, Path.GetRelativePath(Root, file.Path)) : null;
                if (previous != null) { Directory.CreateDirectory(Path.GetDirectoryName(previous)!); File.Copy(file.Path, previous); }
                string? temp = file.Text == null ? null : file.Path + "." + Guid.NewGuid().ToString("N") + ".tmp";
                staged.Add((file.Path, temp, previous)); if (temp != null) File.WriteAllText(temp, file.Text, new UTF8Encoding(false));
            }
            SkinJson.WriteAtomic(Path.Combine(backup, "restore-index.json"), staged.Select(f => new { RelativePath = Path.GetRelativePath(Root, f.Path), Existed = f.Backup != null }));
            SkinJson.WriteAtomic(Path.Combine(backup, "changes.json"), files.Select(f => new { File = Path.GetRelativePath(Root, f.Path), Before = File.Exists(f.Path) ? File.ReadAllText(f.Path) : "", After = f.Text }));
            foreach (var file in staged) { if (file.Temp != null) File.Move(file.Temp, file.Path, true); else File.Delete(file.Path); committed.Add((file.Path, file.Backup)); }
        }
        catch
        {
            foreach (var file in committed.AsEnumerable().Reverse())
                if (file.Backup != null) File.Copy(file.Backup, file.Path, true); else File.Delete(file.Path);
            throw;
        }
        finally { foreach (var file in staged) if (file.Temp != null && File.Exists(file.Temp)) File.Delete(file.Temp); }
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
        => LoadFiles(Schema(id), Custom(id), Path.Combine(Root, $"shortcuts_{id}.dict.yaml"));
    public InputSettings LoadScheme(string schemaId)
    {
        if (schemaId.StartsWith("wanxiang_")) return Load(schemaId[9..]);
        if (schemaId != "wanxiang") throw new InvalidDataException("当前方案不支持万象专属设置。");
        return LoadFiles(Path.Combine(Root, "wanxiang.schema.yaml"), Path.Combine(Root, "wanxiang.custom.yaml"), "");
    }
    InputSettings LoadFiles(string schemaFile, string customFile, string phrases)
    {
        var patch = Map(Read(customFile), "patch"); var schema = Read(schemaFile);
        string schemaId = Path.GetFileName(schemaFile).Replace(".schema.yaml", "");
        var effective = new RimeSettings(Root).EffectiveSchema(schemaId);
        YamlNode? Value(string key) => RimeSettings.ReadNode(patch, key) ?? RimeSettings.ReadNode(effective, key);
        string? ValueText(string key) => (Value(key) as YamlScalarNode)?.Value;
        var result = new InputSettings { EnableCorrection = ValueText("translator/enable_correction") != "false" };
        if (int.TryParse(ValueText("super_comment/candidate_length"), out int limit)) result.AnnotationLength = limit;
        var switches = Switches(schema);
        result.Simplified = ValueText($"switches/@{FindSwitch(switches, "s2s")}/reset") is not ("1" or "2" or "3");
        result.ContextReorder = ValueText($"switches/@{FindSwitch(switches, "context_reorder")}/reset") != "0";
        if (Value("speller/algebra/__patch") is YamlSequenceNode modes && modes.Children.OfType<YamlScalarNode>().FirstOrDefault(n => n.Value?.StartsWith("wanxiang_algebra:/base/") == true) is { } first)
            result.PinyinMode = first.Value?.Split('/').Last() ?? "全拼";
        if (RimeSettings.ReadNode(patch, "speller/algebra/+") is YamlSequenceNode rules)
        {
            var values = rules.Children.OfType<YamlScalarNode>().Select(n => n.Value).ToHashSet();
            result.FuzzyGroups = Fuzzy.Where(pair => pair.Value.Any(values.Contains)).Select(pair => pair.Key).ToList();
        }
        if (File.Exists(phrases)) foreach (var line in File.ReadLines(phrases).SkipWhile(l => l.Trim() != "...").Skip(1)) { var parts = line.Split('\t'); if (parts.Length == 3) result.Shortcuts[parts[1]] = parts[0]; }
        return result;
    }
    public string Apply(string id, InputSettings settings)
        => WriteBatchBackedUp(PreviewScheme("wanxiang_" + id, settings).ToDictionary(c => c.Path, c => c.After!));
    public IReadOnlyList<ConfigChange> PreviewScheme(string schemaId, InputSettings settings)
    {
        bool scene = schemaId.StartsWith("wanxiang_");
        string id = scene ? schemaId[9..] : "";
        if (scene) ValidateId(id); else if (schemaId != "wanxiang") throw new InvalidDataException("当前方案不支持万象设置。");
        var modes = new[] { "全拼", "自然码", "小鹤双拼", "微软双拼", "搜狗双拼", "智能ABC", "紫光双拼", "拼音加加" };
        if (!modes.Contains(settings.PinyinMode) || settings.FuzzyGroups.Any(n => !Fuzzy.ContainsKey(n))) throw new InvalidDataException("不支持的拼音模式或模糊音组。");
        if (settings.Shortcuts.Any(p => !Regex.IsMatch(p.Key, "^[a-z][a-z0-9]{0,31}$") || p.Value.Length is < 1 or > 512 || p.Value.Contains('\n') || p.Value.Contains('\t')))
            throw new InvalidDataException("快捷短语格式无效。");
        string file = scene ? Custom(id) : Path.Combine(Root, "wanxiang.custom.yaml");
        var document = Read(file); var patch = Map(document, "patch");
        var switches = Switches(Read(scene ? Schema(id) : Path.Combine(Root, "wanxiang.schema.yaml")));
        var previous = LoadScheme(schemaId);
        if (previous.AnnotationLength != settings.AnnotationLength)
        {
            if (previous.AnnotationLength == null || settings.AnnotationLength is null or < 1 or > 64)
                throw new InvalidDataException("当前万象版本不支持候选注音长度设置。");
            patch.Children[new YamlScalarNode("super_comment/candidate_length")] = new YamlScalarNode(settings.AnnotationLength.Value.ToString());
        }
        if (previous.Simplified != settings.Simplified)
            patch.Children[new YamlScalarNode($"switches/@{FindSwitch(switches, "s2s")}/reset")] = new YamlScalarNode(settings.Simplified ? "0" : "1");
        if (previous.ContextReorder != settings.ContextReorder)
            patch.Children[new YamlScalarNode($"switches/@{FindSwitch(switches, "context_reorder")}/reset")] = new YamlScalarNode(settings.ContextReorder ? "1" : "0");
        if (previous.EnableCorrection != settings.EnableCorrection)
            patch.Children[new YamlScalarNode("translator/enable_correction")] = new YamlScalarNode(settings.EnableCorrection ? "true" : "false");
        // Preserve rules not owned by this module, including earlier fuzzy spellings.
        var known = Fuzzy.Values.SelectMany(v => v).Concat(new[] { "derive/^z/zh/", "derive/^c/ch/", "derive/^s/sh/" }).ToHashSet();
        var rules = patch.Children.TryGetValue("speller/algebra/+", out var old) && old is YamlSequenceNode oldRules
            ? oldRules.Children.OfType<YamlScalarNode>().Where(n => !known.Contains(n.Value ?? "")).Select(n => new YamlScalarNode(n.Value)).ToList() : [];
        rules.AddRange(settings.FuzzyGroups.Distinct().SelectMany(n => Fuzzy[n]).Select(s => new YamlScalarNode(s)));
        if (!previous.FuzzyGroups.ToHashSet().SetEquals(settings.FuzzyGroups)) patch.Children[new YamlScalarNode("speller/algebra/+")] = new YamlSequenceNode(rules);
        if (previous.PinyinMode != settings.PinyinMode)
        {
            var refs = RimeSettings.ReadNode(patch, "speller/algebra/__patch") is YamlSequenceNode oldRefs ? oldRefs.Children.ToList() : new List<YamlNode>();
            int index = refs.FindIndex(n => n is YamlScalarNode scalar && scalar.Value?.StartsWith("wanxiang_algebra:/base/") == true);
            var reference = new YamlScalarNode("wanxiang_algebra:/base/" + settings.PinyinMode);
            if (index < 0) refs.Insert(0, reference); else refs[index] = reference;
            patch.Children[new YamlScalarNode("speller/algebra/__patch")] = new YamlSequenceNode(refs);
        }
        var changes = new List<ConfigChange>();
        if (scene && !previous.Shortcuts.OrderBy(p => p.Key).SequenceEqual(settings.Shortcuts.OrderBy(p => p.Key)))
        {
            patch.Children[new YamlScalarNode("translator/prism")] = new YamlScalarNode("wanxiang_" + id);
            patch.Children[new YamlScalarNode("custom_phrase/dictionary")] = new YamlScalarNode("shortcuts_" + id);
            patch.Children[new YamlScalarNode("custom_phrase/prism")] = new YamlScalarNode("shortcuts_" + id);
            string phrases = Path.Combine(Root, $"shortcuts_{id}.dict.yaml");
            changes.Add(new(phrases, File.Exists(phrases) ? File.ReadAllText(phrases) : "", ShortcutDictionary(id, settings.Shortcuts)));
        }
        if (changes.Count > 0 || previous.AnnotationLength != settings.AnnotationLength || previous.Simplified != settings.Simplified || previous.ContextReorder != settings.ContextReorder || previous.EnableCorrection != settings.EnableCorrection || previous.PinyinMode != settings.PinyinMode || !previous.FuzzyGroups.ToHashSet().SetEquals(settings.FuzzyGroups))
            changes.Add(new(file, File.Exists(file) ? File.ReadAllText(file) : "", Serialize(document)));
        return changes;
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
        if (RimeSettings.ReadNode(doc, "super_comment/candidate_length") != null) patch.Add("super_comment/candidate_length", "64");
        patch.Add("translator/prism", "wanxiang_" + id);
        patch.Add("add_user_dict/enable_user_dict", "true");
        patch.Add("add_user_dict/enable_auto_phrase", "true");
        patch.Add("custom_phrase/dictionary", "shortcuts_" + id);
        patch.Add("custom_phrase/prism", "shortcuts_" + id);
        patch.Add($"switches/@{FindSwitch(Switches(doc), "s2s")}/reset", "0");
        patch.Add($"switches/@{FindSwitch(Switches(doc), "context_reorder")}/reset", "1");
        patch.Add("translator/packs", new YamlSequenceNode(new YamlScalarNode(id)));
        var defaults = Read(Path.Combine(Root, "default.custom.yaml")); var defaultPatch = Map(defaults, "patch");
        if (!defaultPatch.Children.TryGetValue("schema_list", out var node))
        {
            var reader = new RimeSettings(Root);
            var definition = new SettingDefinition("schemas", "schema_list", "方案列表", "yaml-sequence", "[]", "");
            if (!reader.Read(definition, null).Supported) throw new InvalidDataException("方案列表包含复杂补丁，请先人工审阅。");
            var existing = reader.Schemas().Where(s => s.Enabled).ToList();
            if (existing.Count == 0) throw new InvalidDataException("没有可读取的原方案列表；请先启用基础方案。");
            node = new YamlSequenceNode(existing.Select(s => new YamlMappingNode { { "schema", s.Id } })); defaultPatch.Add("schema_list", node);
        }
        if (node is not YamlSequenceNode list) throw new InvalidDataException("原方案列表格式不支持。");
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
        ValidateId(id);
        var change = PreviewDictionary(Path.Combine(Root, id + ".dict.yaml"), source);
        WriteBackedUp(change.Path, change.After!);
    }
    public ConfigChange PreviewDictionary(string path, string source)
    {
        path = Path.GetFullPath(path);
        if (Path.GetDirectoryName(path) != Root || !path.EndsWith(".dict.yaml", StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("请选择用户目录中的静态 .dict.yaml 词库。");
        if (new FileInfo(source).Length > 20 * 1024 * 1024) throw new InvalidDataException("词库超过20MiB。");
        var rows = new List<string>();
        foreach (string line in File.ReadLines(source))
        {
            if (string.IsNullOrWhiteSpace(line) || line.StartsWith('#')) continue;
            var fields = line.Split('\t');
            if (fields.Length is < 2 or > 3 || !Regex.IsMatch(fields[1], "^[a-züv ']+$") || fields[0].Length > 256 || (fields.Length == 3 && !int.TryParse(fields[2], out _))) throw new InvalidDataException("词库须为：词语<TAB>拼音<TAB>可选词频。");
            rows.Add(line);
        }
        if (!File.Exists(path)) throw new InvalidDataException("场景词库不存在。");
        var lines = File.ReadAllLines(path).ToList(); int marker = lines.FindIndex(s => s.Trim() == "...");
        if (marker < 0) throw new InvalidDataException("原词库头部无效。");
        var merged = lines.Skip(marker + 1).Concat(rows).Where(s => !string.IsNullOrWhiteSpace(s)).Distinct();
        return new(path, File.ReadAllText(path), string.Join('\n', lines.Take(marker + 1).Concat(merged)) + "\n");
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
