using System.Globalization;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using YamlDotNet.RepresentationModel;

namespace Shorekeeper.Core;

public sealed record DictionarySource(string Id, string Name, string Url, string Description);
public sealed record DictionaryRevision(string Hash, string Version, int Entries, DateTimeOffset DownloadedAt);
public sealed class DictionarySubscription
{
    public int SchemaVersion { get; set; } = 1;
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string Url { get; set; } = "";
    public string CurrentHash { get; set; } = "";
    public List<DictionaryRevision> Revisions { get; set; } = [];
    [System.Text.Json.Serialization.JsonIgnore]
    public string DictionaryName => "sub_" + Id;
}

/// <summary>Explicit downloads only. No network requests or writes occur while reading the library.</summary>
public sealed class DictionarySubscriptions
{
    public const int MaxBytes = 16 * 1024 * 1024;
    public string Root { get; }
    public string Library => Path.Combine(Root, "dictionary-subscriptions");
    public static IReadOnlyList<DictionarySource> Catalog { get; } = new[]
    {
        new DictionarySource("moe", "萌娘百科 · 带声调", "https://github.com/suiginko/moetype/releases/latest/download/moe.dict.yaml", "动漫、游戏角色及专有名词；供带声调编码方案使用，启用前检查兼容性。"),
        new DictionarySource("moe_plain", "萌娘百科 · 无声调", "https://github.com/suiginko/moetype/releases/latest/download/toneless_moe.dict.yaml", "同一来源的无声调版本；供普通拼音方案使用，不能直接混用于带声调词典。")
    };
    public DictionarySubscriptions(string root) => Root = Path.GetFullPath(root);
    static void ValidateId(string id)
    {
        if (!Regex.IsMatch(id, "^[a-z][a-z0-9_]{0,31}$")) throw new InvalidDataException("词库标识须为小写字母开头，最多 32 位字母、数字或下划线。");
    }
    string Safe(string relative)
    {
        string path = SkinStore.ResolveAsset(Root, relative.Replace('\\', '/'));
        for (string? p = path; p != null && p.Length > Root.Length; p = Path.GetDirectoryName(p))
            if ((File.Exists(p) || Directory.Exists(p)) && (File.GetAttributes(p) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("词库路径不能经过链接：" + Path.GetFileName(p));
        return path;
    }
    string Index(string id) { ValidateId(id); return Safe($"dictionary-subscriptions/{id}/subscription.json"); }
    string Cache(string id, string hash)
    {
        ValidateId(id);
        if (!Regex.IsMatch(hash, "^[A-F0-9]{64}$")) throw new InvalidDataException("词库摘要无效。");
        return Safe($"dictionary-subscriptions/{id}/{hash}.dict.yaml");
    }
    string Installed(string id) { ValidateId(id); return Safe("sub_" + id + ".dict.yaml"); }
    public IReadOnlyList<DictionarySubscription> List()
    {
        Safe("dictionary-subscriptions/library-check");
        if (!Directory.Exists(Library)) return [];
        return Directory.GetDirectories(Library).Select(Path.GetFileName).OfType<string>().Where(id => File.Exists(Index(id))).Select(Read).OrderBy(s => s.Name).ToList();
    }
    public DictionarySubscription Read(string id)
    {
        if (new FileInfo(Index(id)).Length > 128 * 1024) throw new InvalidDataException("订阅记录超过大小限制。");
        var value = SkinJson.Read<DictionarySubscription>(Index(id));
        if (value.SchemaVersion != 1 || value.Id != id || value.Revisions.Count is < 1 or > 100 || value.Revisions.All(r => r.Hash != value.CurrentHash))
            throw new InvalidDataException("词库订阅记录无效。");
        foreach (var revision in value.Revisions) Cache(id, revision.Hash);
        return value;
    }
    static ConfigChange Change(string path, string? after) => new(path, File.Exists(path) ? File.ReadAllText(path) : "", after);
    static string Json(DictionarySubscription subscription) => JsonSerializer.Serialize(subscription, SkinJson.Options);
    public static Uri ValidateUrl(string url)
    {
        if (url.Length > 2048 || !Uri.TryCreate(url, UriKind.Absolute, out var uri) || uri.Scheme != "https" || uri.UserInfo.Length != 0 || uri.Fragment.Length != 0 || uri.IsLoopback)
            throw new InvalidDataException("订阅地址须为不含账号密码的 HTTPS 词库直链。");
        return uri;
    }
    public static async Task<byte[]> Download(HttpClient client, string url, CancellationToken ct = default)
    {
        var uri = ValidateUrl(url);
        for (int hop = 0; hop < 6; hop++)
        {
            using var request = new HttpRequestMessage(HttpMethod.Get, uri);
            request.Headers.UserAgent.ParseAdd("RimeSettingsCenter/1.0");
            using var response = await client.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, ct);
            if (response.StatusCode is HttpStatusCode.MovedPermanently or HttpStatusCode.Found or HttpStatusCode.SeeOther or HttpStatusCode.TemporaryRedirect or HttpStatusCode.PermanentRedirect)
            {
                uri = ValidateUrl(new Uri(uri, response.Headers.Location ?? throw new IOException("重定向没有目标地址。")).AbsoluteUri);
                continue;
            }
            if (!response.IsSuccessStatusCode) throw new IOException($"词库下载失败：HTTP {(int)response.StatusCode}。");
            if (response.Content.Headers.ContentLength is > MaxBytes) throw new InvalidDataException("词库超过 16 MiB。");
            await using var stream = await response.Content.ReadAsStreamAsync(ct);
            using var output = new MemoryStream(); byte[] buffer = new byte[32768];
            int read;
            while ((read = await stream.ReadAsync(buffer, ct)) > 0)
            {
                if (output.Length + read > MaxBytes) throw new InvalidDataException("词库超过 16 MiB。");
                output.Write(buffer, 0, read);
            }
            return output.ToArray();
        }
        throw new IOException("词库下载重定向次数过多。");
    }
    public ConfigurationPlan PreviewImport(string id, string name, string url, byte[] bytes)
    {
        ValidateId(id);
        if (string.IsNullOrWhiteSpace(name) || name.Length > 100) throw new InvalidDataException("请填写不超过 100 字的词库名称。");
        if (url.Length != 0) ValidateUrl(url);
        var dictionary = DictionaryText.Parse(bytes);
        string hash = Hash(dictionary.Text);
        string index = Index(id);
        var subscription = File.Exists(index) ? Read(id) : new DictionarySubscription { Id = id, Name = name, Url = url };
        if (subscription.Url != url) throw new InvalidDataException("同标识的来源不同，请使用新的标识，避免替换其他订阅。");
        if (subscription.CurrentHash == hash) return new([]);
        if (subscription.Revisions.Count >= 100 && subscription.Revisions.All(r => r.Hash != hash)) throw new InvalidDataException("版本数量达到上限，请保留所需版本后另建订阅。");
        var changes = new List<ConfigChange>();
        string installed = Installed(id);
        if (File.Exists(installed))
        {
            RequireOwned(subscription);
            foreach (string schema in EnabledSchemas(id)) RequireCompatible(schema, dictionary);
            changes.Add(Change(installed, dictionary.Render(subscription.DictionaryName)));
        }
        subscription.Name = name; subscription.CurrentHash = hash;
        if (subscription.Revisions.All(r => r.Hash != hash)) subscription.Revisions.Insert(0, new(hash, dictionary.Version, dictionary.Entries, DateTimeOffset.UtcNow));
        string cache = Cache(id, hash);
        if (!File.Exists(cache)) changes.Add(Change(cache, dictionary.Text));
        else if (File.ReadAllText(cache) != dictionary.Text) throw new IOException("缓存内容与已保存版本不一致，请先检查文件。");
        changes.Add(Change(index, Json(subscription)));
        return new(changes);
    }
    DictionaryText Current(DictionarySubscription value, string? hash = null)
    {
        string path = Cache(value.Id, hash ?? value.CurrentHash);
        if (new FileInfo(path).Length > MaxBytes) throw new InvalidDataException("缓存词库超过大小限制。");
        var dictionary = DictionaryText.Parse(File.ReadAllBytes(path));
        if (Hash(dictionary.Text) != (hash ?? value.CurrentHash)) throw new IOException("缓存摘要不符，保留当前配置，请重新获取可信词库。");
        return dictionary;
    }
    static string Hash(string text) => Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(text)));
    public IReadOnlyList<string> EnabledSchemas(string id)
    {
        ValidateId(id);
        return Directory.Exists(Root) ? Directory.GetFiles(Root, "*.custom.yaml").Select(p => Path.GetFileName(p)[..^12]).Where(s => IsEnabled(id, s)).ToList() : [];
    }
    void RequireOwned(DictionarySubscription subscription)
    {
        if (File.ReadAllText(Installed(subscription.Id)) != Current(subscription).Render(subscription.DictionaryName))
            throw new IOException("已安装词库被手动修改，请先导出个人修改；本次不会覆盖。");
    }
    public ConfigurationPlan PreviewRollback(string id, string hash)
    {
        var subscription = Read(id);
        if (subscription.Revisions.All(r => r.Hash != hash)) throw new InvalidDataException("请选择已有版本。");
        if (hash == subscription.CurrentHash) return new([]);
        var dictionary = Current(subscription, hash);
        var changes = new List<ConfigChange>();
        foreach (string schema in EnabledSchemas(id)) RequireCompatible(schema, dictionary);
        if (File.Exists(Installed(id))) { RequireOwned(subscription); changes.Add(Change(Installed(id), dictionary.Render(subscription.DictionaryName))); }
        subscription.CurrentHash = hash; changes.Add(Change(Index(id), Json(subscription))); return new(changes);
    }
    public void Export(string id, string destination)
    {
        var subscription = Read(id);
        File.WriteAllText(destination, Current(subscription).Render(subscription.DictionaryName), new UTF8Encoding(false));
    }

    public ConfigurationPlan PreviewRemove(string id)
    {
        var subscription = Read(id);
        // References outside this page are also protected; never delete a dictionary still mentioned in editable YAML.
        if (Directory.GetFiles(Root, "*.yaml").Any(p => p != Installed(id) && File.ReadAllText(Safe(Path.GetFileName(p))).Contains(subscription.DictionaryName, StringComparison.Ordinal)))
            throw new InvalidDataException("词库仍被方案或其他词典引用，请先停用或移除原有引用。");
        var changes = new List<ConfigChange> { Change(Index(id), null) };
        if (File.Exists(Installed(id))) { RequireOwned(subscription); changes.Add(Change(Installed(id), null)); }
        return new(changes);
    }

    public bool IsEnabled(string id, string? schema)
    {
        if (schema == null) return false;
        RimeSettings.ValidateSchemaId(schema);
        string file = Safe(schema + ".custom.yaml");
        var patch = RimeSettings.ReadNode(RimeSettings.Parse(File.Exists(file) ? File.ReadAllText(file) : ""), "patch");
        return RimeSettings.ReadNode(patch!, "translator/packs/+") is YamlSequenceNode list && list.Children.OfType<YamlScalarNode>().Any(n => n.Value == "sub_" + id);
    }
    public ConfigurationPlan PreviewEnable(string id, string schema, bool enabled)
    {
        RimeSettings.ValidateSchemaId(schema); var subscription = Read(id);
        string custom = Safe(schema + ".custom.yaml"); string before = File.Exists(custom) ? File.ReadAllText(custom) : "";
        var doc = RimeSettings.Parse(before);
        if (!doc.Children.TryGetValue("patch", out var patchNode)) doc.Add("patch", patchNode = new YamlMappingNode());
        var patch = patchNode as YamlMappingNode ?? throw new InvalidDataException("patch 不是映射。");
        const string key = "translator/packs/+";
        // Do not flatten or replace another author's nested/reference patches.
        foreach (string conflict in new[] { "translator", "translator/packs", "translator/packs/__patch" })
            if (patch.Children.ContainsKey(conflict)) throw new InvalidDataException("方案包含复杂 packs 补丁，请先在原配置中管理，订阅保持已下载状态。");
        var current = patch.Children.TryGetValue(key, out var node) ? node as YamlSequenceNode ?? throw new InvalidDataException("packs 追加补丁必须为列表。") : new YamlSequenceNode();
        if (current.Children.Any(n => n is not YamlScalarNode)) throw new InvalidDataException("packs 列表格式不受支持。");
        var changes = new List<ConfigChange>();
        if (enabled)
        {
            var dictionary = Current(subscription);
            RequireCompatible(schema, dictionary);
            if (File.Exists(Installed(id))) RequireOwned(subscription);
            changes.Add(Change(Installed(id), dictionary.Render(subscription.DictionaryName)));
            if (!current.Children.OfType<YamlScalarNode>().Any(n => n.Value == subscription.DictionaryName)) current.Add(subscription.DictionaryName);
        }
        else foreach (var entry in current.Children.OfType<YamlScalarNode>().Where(n => n.Value == subscription.DictionaryName).ToList()) current.Children.Remove(entry);
        if (current.Children.Count == 0) patch.Children.Remove(new YamlScalarNode(key)); else patch.Children[new YamlScalarNode(key)] = current;
        var writer = new StringWriter(); new YamlStream(new YamlDocument(doc)).Save(writer, false);
        changes.Add(new(custom, before, writer.ToString()));
        return new(changes.Where(c => c.Before != c.After).ToList());
    }
    void RequireCompatible(string schema, DictionaryText incoming)
    {
        var effective = new RimeSettings(Root).EffectiveSchema(schema);
        string custom = Safe(schema + ".custom.yaml");
        var patch = RimeSettings.ReadNode(RimeSettings.Parse(File.Exists(custom) ? File.ReadAllText(custom) : ""), "patch");
        var translators = RimeSettings.ReadNode(effective, "engine/translators") as YamlSequenceNode;
        if (translators == null || !translators.Children.OfType<YamlScalarNode>().Any(n => n.Value == "script_translator"))
            throw new InvalidDataException("当前页只适配主 script_translator；其他翻译器可下载或导出词库，暂不自动挂载。");
        string? main = ((RimeSettings.ReadNode(patch!, "translator/dictionary") ?? RimeSettings.ReadNode(effective, "translator/dictionary")) as YamlScalarNode)?.Value;
        if (string.IsNullOrWhiteSpace(main)) throw new InvalidDataException("未找到方案主词典，不能自动启用。");
        var needed = new HashSet<string>(incoming.Codes, StringComparer.Ordinal);
        var visited = new HashSet<string>(StringComparer.OrdinalIgnoreCase); long total = 0;
        void Visit(string dictionary)
        {
            if (needed.Count == 0 || !visited.Add(dictionary)) return;
            if (visited.Count > 32) throw new InvalidDataException("主词典引用过多，请手动适配。");
            string path = Safe(dictionary + ".dict.yaml");
            if (!File.Exists(path)) throw new InvalidDataException("缺少主词典源码，无法验证编码兼容：" + dictionary + "；仍可下载或导出词库。");
            total += new FileInfo(path).Length;
            if (total > 128L * 1024 * 1024) throw new InvalidDataException("编码校验数据超过 128 MiB，请手动适配。");
            using var reader = File.OpenText(path); var header = new StringBuilder(); string? line; bool ended = false;
            while ((line = reader.ReadLine()) != null)
            {
                if (line.Trim() == "...") { ended = true; break; }
                if (header.Length + line.Length > 65536) throw new InvalidDataException("主词典表头过大。");
                header.AppendLine(line);
            }
            if (!ended) throw new InvalidDataException("主词典缺少表头结束标记。");
            var info = RimeSettings.Parse(header.ToString());
            int codeColumn = DictionaryText.CodeColumn(info);
            while (needed.Count > 0 && (line = reader.ReadLine()) != null)
            {
                if (line.StartsWith('#')) continue;
                var fields = line.Split('\t');
                if (fields.Length > codeColumn) foreach (string code in fields[codeColumn].Split(' ', StringSplitOptions.RemoveEmptyEntries)) needed.Remove(code);
            }
            if (RimeSettings.ReadNode(info, "import_tables") is YamlSequenceNode tables)
                foreach (var table in tables.Children.OfType<YamlScalarNode>()) if (table.Value != null) Visit(table.Value);
        }
        Visit(main);
        if (needed.Count != 0) throw new InvalidDataException($"有 {needed.Count} 种编码不在主词典音节表中（例如 {string.Join("、", needed.Take(5))}）；请选匹配的带调／无调版本或先用万象工具转换，不会猜测读音。");
    }
}

internal sealed record DictionaryText(string Text, string Version, int Entries, HashSet<string> Codes, string Body)
{
    public static int CodeColumn(YamlMappingNode header)
    {
        if (RimeSettings.ReadNode(header, "columns") is not YamlSequenceNode columns) return 1;
        var names = columns.Children.OfType<YamlScalarNode>().Select(c => c.Value).ToList();
        if (names.Count != columns.Children.Count || names.Distinct().Count() != names.Count || !names.Contains("text") || !names.Contains("code")) throw new InvalidDataException("词库 columns 必须包含 text 和 code。");
        return names.IndexOf("code");
    }
    public static DictionaryText Parse(byte[] bytes)
    {
        if (bytes.Length is < 1 or > DictionarySubscriptions.MaxBytes) throw new InvalidDataException("词库须为 1 字节至 16 MiB 的 UTF-8 .dict.yaml 文件。");
        string text;
        try { text = new UTF8Encoding(false, true).GetString(bytes).TrimStart('\uFEFF'); }
        catch (DecoderFallbackException) { throw new InvalidDataException("词库不是有效 UTF-8。"); }
        using var reader = new StringReader(text); var headerText = new StringBuilder(); string? line; bool ended = false;
        while ((line = reader.ReadLine()) != null)
        {
            if (line.Trim() == "...") { ended = true; break; }
            if (headerText.Length + line.Length > 65536) throw new InvalidDataException("词库表头超过 64 KiB。");
            headerText.AppendLine(line);
        }
        if (!ended) throw new InvalidDataException("词库缺少 YAML 表头结束标记 ...。");
        var header = RimeSettings.Parse(headerText.ToString());
        if (RimeSettings.ReadNode(header, "name") is not YamlScalarNode name || string.IsNullOrWhiteSpace(name.Value)) throw new InvalidDataException("词库缺少名称。");
        if (RimeSettings.ReadNode(header, "import_tables") != null || RimeSettings.ReadNode(header, "encoder") != null || RimeSettings.ReadNode(header, "use_preset_vocabulary") is YamlScalarNode preset && preset.Value != "false")
            throw new InvalidDataException("仅接受自包含静态词库，不执行引用词典、编码器或预设词汇配置。");
        int codeColumn = CodeColumn(header), textColumn = 0, weightColumn = 2;
        if (RimeSettings.ReadNode(header, "columns") is YamlSequenceNode columns)
        {
            var names = columns.Children.OfType<YamlScalarNode>().Select(c => c.Value).ToList();
            if (names.Any(n => n is not ("text" or "code" or "weight"))) throw new InvalidDataException("首版只支持 text、code、weight 列。");
            textColumn = names.IndexOf("text"); weightColumn = names.IndexOf("weight");
        }
        string version = (RimeSettings.ReadNode(header, "version") as YamlScalarNode)?.Value ?? "未标注";
        if (version.Length > 100) throw new InvalidDataException("词库版本过长。");
        int entries = 0; var codes = new HashSet<string>(StringComparer.Ordinal); var body = new StringBuilder();
        while ((line = reader.ReadLine()) != null)
        {
            if (string.IsNullOrWhiteSpace(line) || line.StartsWith('#')) continue;
            if (line.Length > 2048 || line.Any(c => char.IsControl(c) && c != '\t')) throw new InvalidDataException("词条过长或包含控制字符。");
            var fields = line.Split('\t');
            if (fields.Length <= Math.Max(codeColumn, textColumn) || string.IsNullOrWhiteSpace(fields[textColumn]) || string.IsNullOrWhiteSpace(fields[codeColumn])) throw new InvalidDataException("词条缺少文字或编码；两列须用 Tab 分隔。");
            var syllables = fields[codeColumn].Split(' ', StringSplitOptions.RemoveEmptyEntries);
            if (syllables.Any(s => s.Length > 64 || s.Any(c => !char.IsLetterOrDigit(c) && c is not (';' or '_' or '\'')))) throw new InvalidDataException("词条编码不受支持。");
            foreach (string syllable in syllables) codes.Add(syllable);
            string weight = weightColumn >= 0 && fields.Length > weightColumn ? fields[weightColumn] : "100";
            if (!double.TryParse(weight, NumberStyles.Float, CultureInfo.InvariantCulture, out double number) || !double.IsFinite(number) || number < 0) throw new InvalidDataException("词频须为非负数字。");
            body.Append(fields[textColumn]).Append('\t').AppendJoin(' ', syllables).Append('\t').Append(weight).Append('\n');
            if (++entries > 500000 || codes.Count > 100000) throw new InvalidDataException("词条或编码数量超过限制。");
        }
        if (entries == 0) throw new InvalidDataException("词库没有有效词条。");
        return new(text, version, entries, codes, body.ToString());
    }
    public string Render(string name) => $"# Managed by Rime Settings Center; keep personal words in a separate dictionary.\n---\nname: {name}\nversion: {JsonSerializer.Serialize(Version)}\nsort: by_weight\ncolumns: [text, code, weight]\n...\n{Body}";
}
