using System.Text.RegularExpressions;
using YamlDotNet.RepresentationModel;

namespace Shorekeeper.Core;

public sealed record SettingsModule(string Id, string Name, string Description);
public static class SettingsModules
{
    public static IReadOnlyList<SettingsModule> All { get; } = new[]
    {
        new SettingsModule("general", "通用与候选显示", "管理中英文切换、方案快捷键与候选显示。"),
        new SettingsModule("schemas", "输入方案", "选择启用的方案，调整方案顺序并查看依赖。"),
        new SettingsModule("wanxiang", "万象输入", "兼容万象现有能力；专属选项按方案能力显示。"),
        new SettingsModule("keys", "按键与标点", "为全局或指定方案设置按键和全半角标点。"),
        new SettingsModule("dictionary", "词库与模型", "导入自定义词库，管理官方快照与语法模型。"),
        new SettingsModule("subscriptions", "扩展词库与订阅", "下载游戏、动漫等扩展词库，按方案启用并管理更新与回退。"),
        new SettingsModule("skins", "皮肤", "独立导入、预览与选择皮肤，不修改输入方案。"),
        new SettingsModule("scenes", "场景", "工作与日常共用基础词库，分别保存学习数据。"),
        new SettingsModule("backup", "备份与恢复", "审阅配置修改，恢复保存前的配置。"),
        new SettingsModule("sync", "同步", "按需传输官方快照，再交由 Rime 合并。"),
        new SettingsModule("diagnostics", "诊断", "问题1：先保存现场，再恢复。")
    };
}

public sealed record SettingDefinition(string Module, string Key, string Label, string Kind,
    string DefaultValue, string Help, string[]? Choices = null, bool WeaselOnly = false, bool GlobalOnly = false);
public sealed record SettingValue(SettingDefinition Definition, string Value, string Source, bool Supported = true);
public sealed record SchemaInfo(string Id, string Name, string Version, string Author, string Dependencies, string Source, bool Enabled);
public sealed record ConfigChange(string Path, string Before, string? After);
public sealed class ConfigurationPlan(IReadOnlyList<ConfigChange> changes)
{
    public IReadOnlyList<ConfigChange> Changes { get; } = changes;
    public string Save(string root)
    {
        foreach (var change in Changes)
            if ((File.Exists(change.Path) ? File.ReadAllText(change.Path) : "") != change.Before)
                throw new IOException("配置在预览后发生变化，请重新载入：" + Path.GetFileName(change.Path));
        if (Changes.Count == 0) throw new InvalidOperationException("没有需要保存的修改。");
        string backup = new RimeProfiles(root).WriteChangesBackedUp(Changes.ToDictionary(c => c.Path, c => c.After));
        return backup;
    }
}

/// <summary>Rime patch access preserves unknown nodes; compiled configs are read-only fallbacks.</summary>
public sealed class RimeSettings
{
    public string Root { get; }
    readonly Dictionary<string, string> loadedFiles = new(StringComparer.OrdinalIgnoreCase);
    static readonly string[] SwitchChoices = ["noop", "inline_ascii", "commit_text", "commit_code", "clear"];
    public static IReadOnlyList<SettingDefinition> Definitions { get; } = new[]
    {
        new SettingDefinition("general", "ascii_composer/switch_key/Shift_L", "左 Shift", "choice", "inline_ascii", "中英文切换行为：内嵌英文、上屏文字、上屏编码、清空或不操作。", SwitchChoices),
        new SettingDefinition("general", "ascii_composer/switch_key/Shift_R", "右 Shift", "choice", "commit_text", "独立配置右侧 Shift。", SwitchChoices),
        new SettingDefinition("general", "ascii_composer/good_old_caps_lock", "Caps Lock 切换英文", "bool", "true", "使用 Rime 的 Caps Lock 切换逻辑。"),
        new SettingDefinition("general", "switcher/hotkeys", "方案切换快捷键", "yaml-sequence", "[Control+grave, F4]", "Rime 快捷键列表，例如 [Control+grave, F4]。", GlobalOnly: true),
        new SettingDefinition("general", "menu/page_size", "每页候选数", "number", "5", "范围 1—10；当前方案可以覆盖全局默认。"),
        new SettingDefinition("general", "style/horizontal", "横排候选", "bool", "true", "小狼毫原生候选窗设置；系统应用自行绘制的候选列表可能不同。", WeaselOnly: true),
        new SettingDefinition("keys", "key_binder/bindings", "按键绑定", "yaml-sequence", "[]", "Rime 列表格式；每项包含 when、accept，以及 send、toggle 或 set 等动作。"),
        new SettingDefinition("keys", "punctuator/half_shape", "半角标点", "yaml-map", "{}", "Rime 标点映射；字符可以映射到文本、列表或合法动作映射。"),
        new SettingDefinition("keys", "punctuator/full_shape", "全角标点", "yaml-map", "{}", "与半角标点分别设置，支持方案专属补丁。"),
        new SettingDefinition("dictionary", "grammar/language", "语法模型", "model", "", "选择用户目录中的 .gram 文件；空值移除当前覆盖，恢复继承。")
    };
    public RimeSettings(string root) { Root = Path.GetFullPath(root); }
    public static void ValidateSchemaId(string id)
    {
        if (!Regex.IsMatch(id, "^[a-zA-Z0-9][a-zA-Z0-9_-]{0,95}$")) throw new InvalidDataException("方案标识无效。");
    }
    string Custom(string? schema, bool weasel = false)
    {
        if (schema != null) ValidateSchemaId(schema);
        return Path.Combine(Root, weasel ? "weasel.custom.yaml" : schema == null ? "default.custom.yaml" : schema + ".custom.yaml");
    }
    string Text(string path) => File.Exists(path) ? File.ReadAllText(path) : "";
    void Remember(string path) => loadedFiles.TryAdd(path, Text(path));
    public static YamlMappingNode Parse(string text)
    {
        if (string.IsNullOrWhiteSpace(text)) return new();
        var stream = new YamlStream(); stream.Load(new StringReader(text));
        return stream.Documents.Single().RootNode as YamlMappingNode ?? throw new InvalidDataException("配置必须是一个 YAML 映射。");
    }
    static YamlNode? At(YamlNode? node, string path)
    {
        if (node == null || path.Length == 0) return node;
        if (node is YamlMappingNode map && map.Children.TryGetValue(new YamlScalarNode(path), out var exact)) return exact;
        if (node is YamlMappingNode flat)
            foreach (var entry in flat.Children.OrderByDescending(p => (p.Key as YamlScalarNode)?.Value?.Length ?? 0))
                if (entry.Key is YamlScalarNode key && key.Value != null && path.StartsWith(key.Value + "/", StringComparison.Ordinal))
                    return At(entry.Value, path[(key.Value.Length + 1)..]);
        var parts = path.Split('/', 2);
        if (node is YamlMappingNode mapping && mapping.Children.TryGetValue(new YamlScalarNode(parts[0]), out var child))
            return At(child, parts.Length == 1 ? "" : parts[1]);
        if (node is YamlSequenceNode sequence && parts[0].StartsWith('@') && int.TryParse(parts[0][1..], out int index) && index >= 0 && index < sequence.Children.Count)
            return At(sequence.Children[index], parts.Length == 1 ? "" : parts[1]);
        return null;
    }
    public static YamlNode? ReadNode(YamlNode node, string path) => At(node, path);
    static string Format(YamlNode node)
    {
        if (node is YamlScalarNode scalar) return scalar.Value ?? "";
        var writer = new StringWriter(); new YamlStream(new YamlDocument(node)).Save(writer, false);
        return writer.ToString().Trim();
    }
    static string Serialize(YamlMappingNode node) { var writer = new StringWriter(); new YamlStream(new YamlDocument(node)).Save(writer, false); return writer.ToString(); }
    static YamlMappingNode Patch(YamlMappingNode doc)
    {
        if (!doc.Children.TryGetValue("patch", out var node)) { var p = new YamlMappingNode(); doc.Add("patch", p); return p; }
        return node as YamlMappingNode ?? throw new InvalidDataException("patch 必须是 YAML 映射。");
    }
    public SettingValue Read(SettingDefinition definition, string? schema)
    {
        if (definition.WeaselOnly && schema != null) return new(definition, "", "小狼毫外观设置只作用于全局。", false);
        if (definition.GlobalOnly && schema != null) return new(definition, "", "此设置只作用于全局默认。", false);
        if (definition.Kind == "model" && (schema == null || At(EffectiveSchema(schema), "grammar") == null))
            return new(definition, "", "当前方案没有声明 grammar，不能强行接入模型。", false);
        var paths = new List<(string Path, bool Patch)>();
        if (definition.WeaselOnly)
        {
            paths.Add((Custom(null, true), true)); paths.Add((Path.Combine(Root, "build", "weasel.yaml"), false)); paths.Add((Path.Combine(Root, "weasel.yaml"), false));
        }
        else
        {
            if (schema != null)
            {
                paths.Add((Custom(schema), true)); paths.Add((Path.Combine(Root, "build", schema + ".schema.yaml"), false)); paths.Add((Path.Combine(Root, schema + ".schema.yaml"), false));
            }
            paths.Add((Custom(null), true)); paths.Add((Path.Combine(Root, "build", "default.yaml"), false)); paths.Add((Path.Combine(Root, "default.yaml"), false));
        }
        // Watch every editable source, including currently absent custom files.
        foreach (var (path, patch) in paths.Where(p => p.Patch)) Remember(path);
        foreach (var (path, patch) in paths)
        {
            if (!File.Exists(path)) continue;
            var doc = Parse(Text(path)); var value = At(patch ? At(doc, "patch") : doc, definition.Key);
            if (patch && (At(At(doc, "patch"), definition.Key + "/+") != null || At(At(doc, "patch"), definition.Key + "/__patch") != null))
                return new(definition, "", "此项含追加或引用补丁；请保留原配置，暂不使用整项编辑。", false);
            if (value != null) return new(definition, Format(value), Path.GetRelativePath(Root, path));
        }
        return new(definition, definition.DefaultValue, "工具默认值（尚无对应配置）");
    }
    public YamlMappingNode EffectiveSchema(string id)
    {
        ValidateSchemaId(id); string compiled = Path.Combine(Root, "build", id + ".schema.yaml");
        return Parse(Text(File.Exists(compiled) ? compiled : Path.Combine(Root, id + ".schema.yaml")));
    }
    public IReadOnlyList<SchemaInfo> Schemas()
    {
        var schemaList = new SettingDefinition("schemas", "schema_list", "启用方案", "yaml-sequence", "[]", "");
        var enabledText = Read(schemaList, null).Value;
        var enabledStream = new YamlStream(); enabledStream.Load(new StringReader(enabledText));
        var enabled = (enabledStream.Documents[0].RootNode as YamlSequenceNode)?.Children.Select(n => (At(n, "schema") as YamlScalarNode)?.Value).OfType<string>().ToList() ?? [];
        var files = Directory.Exists(Root) ? Directory.GetFiles(Root, "*.schema.yaml").Concat(Directory.Exists(Path.Combine(Root, "build")) ? Directory.GetFiles(Path.Combine(Root, "build"), "*.schema.yaml") : []) : [];
        var result = new Dictionary<string, SchemaInfo>();
        foreach (string file in files.OrderBy(f => f.Contains(Path.DirectorySeparatorChar + "build" + Path.DirectorySeparatorChar)))
        {
            var doc = Parse(Text(file)); string id = Format(At(doc, "schema/schema_id") ?? new YamlScalarNode(Path.GetFileName(file).Replace(".schema.yaml", "")));
            ValidateSchemaId(id);
            result.TryAdd(id, new(id, Format(At(doc, "schema/name") ?? new YamlScalarNode(id)), Format(At(doc, "schema/version") ?? new YamlScalarNode("")),
                Format(At(doc, "schema/author") ?? new YamlScalarNode("")), Format(At(doc, "schema/dependencies") ?? new YamlSequenceNode()), Path.GetRelativePath(Root, file), enabled.Contains(id)));
        }
        // Keep unresolved enabled schemas visible so saving cannot silently drop them.
        foreach (string id in enabled) { ValidateSchemaId(id); result.TryAdd(id, new(id, id, "", "", "未找到方案文件", "缺少文件", true)); }
        return result.Values.OrderBy(s => s.Enabled ? enabled.IndexOf(s.Id) : int.MaxValue).ThenBy(s => s.Name).ToList();
    }
    public IReadOnlyList<string> Models() => Directory.Exists(Root) ? Directory.GetFiles(Root, "*.gram").Select(f => Path.GetFileNameWithoutExtension(f)).Order().ToList() : [];
    static YamlNode ParseValue(SettingDefinition definition, string value)
    {
        if (definition.Kind is "text" or "model" or "choice")
        {
            if (definition.Choices != null && !definition.Choices.Contains(value)) throw new InvalidDataException("选项无效：" + definition.Label);
            return new YamlScalarNode(value);
        }
        if (definition.Kind == "bool" && value is not ("true" or "false")) throw new InvalidDataException("布尔值无效。");
        if (definition.Kind == "number" && (!int.TryParse(value, out int count) || count is < 1 or > 10)) throw new InvalidDataException("候选数量须为 1—10。");
        var stream = new YamlStream(); stream.Load(new StringReader(value));
        if (stream.Documents.Count != 1) throw new InvalidDataException("请填写单个 YAML 值。");
        var node = stream.Documents[0].RootNode;
        if (definition.Kind == "yaml-sequence" && node is not YamlSequenceNode || definition.Kind == "yaml-map" && node is not YamlMappingNode) throw new InvalidDataException("列表或映射格式无效：" + definition.Label);
        if (definition.Key == "switcher/hotkeys" && node is YamlSequenceNode hotkeys && hotkeys.Children.Any(n => n is not YamlScalarNode || string.IsNullOrWhiteSpace(((YamlScalarNode)n).Value))) throw new InvalidDataException("快捷键须为非空字符串列表。");
        if (definition.Key == "key_binder/bindings" && node is YamlSequenceNode bindings && bindings.Children.Any(n => n is not YamlMappingNode || At(n, "when") == null || At(n, "accept") == null || new[] { "send", "toggle", "set", "unset", "select" }.All(k => At(n, k) == null))) throw new InvalidDataException("按键绑定缺少 when、accept 或动作。");
        return node;
    }
    static void RemoveNested(YamlMappingNode patch, string key)
    {
        patch.Children.Remove(new YamlScalarNode(key));
        foreach (var entry in patch.Children.ToList())
            if (entry.Key is YamlScalarNode prefix && prefix.Value != null && key.StartsWith(prefix.Value + "/", StringComparison.Ordinal) && entry.Value is YamlMappingNode subtree)
            { RemoveNested(subtree, key[(prefix.Value.Length + 1)..]); if (subtree.Children.Count == 0) patch.Children.Remove(entry.Key); }
        var parts = key.Split('/', 2);
        if (parts.Length == 2 && patch.Children.TryGetValue(parts[0], out var node) && node is YamlMappingNode map)
        { RemoveNested(map, parts[1]); if (map.Children.Count == 0) patch.Children.Remove(new YamlScalarNode(parts[0])); }
    }
    public ConfigurationPlan Preview(string? schema, IEnumerable<(SettingDefinition Definition, string Value)> edits)
    {
        var docs = new Dictionary<string, YamlMappingNode>();
        foreach (var (definition, value) in edits)
        {
            if (!Read(definition, schema).Supported) throw new InvalidDataException("当前范围不支持：" + definition.Label);
            if (definition.Kind == "model" && value.Length > 0 && !Models().Contains(value)) throw new InvalidDataException("请先导入对应 .gram 模型。");
            string file = Custom(schema, definition.WeaselOnly);
            if (loadedFiles.TryGetValue(file, out string? before) && before != Text(file)) throw new IOException("配置已被其他程序修改，请重新载入。");
            if (!docs.TryGetValue(file, out var doc)) docs[file] = doc = Parse(Text(file));
            var patch = Patch(doc); RemoveNested(patch, definition.Key);
            if (definition.Kind != "model" || value.Length > 0) patch.Children[new YamlScalarNode(definition.Key)] = ParseValue(definition, value);
        }
        return new(docs.Select(p => new ConfigChange(p.Key, Text(p.Key), Serialize(p.Value))).Where(c => c.Before != c.After).ToList());
    }
    public ConfigurationPlan PreviewSchemas(IEnumerable<string> ids)
    {
        var list = ids.ToList(); if (list.Count == 0 || list.Distinct().Count() != list.Count) throw new InvalidDataException("至少启用一个方案，且不能重复。");
        foreach (string id in list) ValidateSchemaId(id);
        string value = Format(new YamlSequenceNode(list.Select(id => new YamlMappingNode { { "schema", id } })));
        return Preview(null, [(new("schemas", "schema_list", "启用方案", "yaml-sequence", "[]", ""), value)]);
    }
    public string ImportModel(string source)
    {
        if (Path.GetExtension(source) != ".gram" || new FileInfo(source).Length is < 1 or > 512L * 1024 * 1024) throw new InvalidDataException("请选择不超过 512MiB 的 .gram 文件。");
        string name = Path.GetFileName(source);
        if (!SkinStore.IsId(Path.GetFileNameWithoutExtension(name))) throw new InvalidDataException("模型文件名无效。");
        string target = Path.Combine(Root, name); if (File.Exists(target)) throw new IOException("同名模型已存在；请先比较版本或更改文件名。");
        Directory.CreateDirectory(Root); string stage = target + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try { File.Copy(source, stage); File.Move(stage, target); } finally { if (File.Exists(stage)) File.Delete(stage); }
        return target;
    }
    public static ConfigurationPlan PreviewRestore(string root, string backup)
    {
        string allowed = Path.GetFullPath(Path.Combine(root, "backups", "shorekeeper")) + Path.DirectorySeparatorChar;
        if (!Path.GetFullPath(backup).StartsWith(allowed, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("请选择本目录生成的配置备份。");
        var index = SkinJson.Read<List<RestoreFile>>(Path.Combine(backup, "restore-index.json"));
        var changes = index.Select(f =>
        {
            string target = SkinStore.ResolveAsset(root, f.RelativePath.Replace('\\', '/'));
            return new ConfigChange(target, File.Exists(target) ? File.ReadAllText(target) : "", f.Existed ? File.ReadAllText(SkinStore.ResolveAsset(backup, f.RelativePath.Replace('\\', '/'))) : null);
        }).Where(c => c.After == null ? File.Exists(c.Path) : c.Before != c.After).ToList();
        return new(changes);
    }
    public static void Restore(string root, string backup) => PreviewRestore(root, backup).Save(root);
    public sealed class RestoreFile { public string RelativePath { get; set; } = ""; public bool Existed { get; set; } }
}
