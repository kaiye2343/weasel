using System.Net;
using System.Net.Http.Headers;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Xml;
using System.Xml.Linq;

namespace Shorekeeper.Core;
public sealed record DavFile(byte[] Content, string? ETag);
public interface IWebDav
{
    Task<IReadOnlyList<string>> Directories(string path, CancellationToken ct);
    Task<DavFile?> Get(string path, CancellationToken ct);
    Task MakeDirectory(string path, CancellationToken ct);
    Task Put(string path, byte[] content, string? expectedETag, bool createOnly, CancellationToken ct);
}
public sealed class HttpWebDav : IWebDav, IDisposable
{
    readonly HttpClient client;
    readonly Uri root;
    public HttpWebDav(Uri endpoint, string username, string password, HttpMessageHandler? handler = null)
    {
        if (endpoint.Scheme != "https" || !string.IsNullOrEmpty(endpoint.UserInfo) || !string.IsNullOrEmpty(endpoint.Query)) throw new ArgumentException("WebDAV只接受不含凭据的HTTPS地址。");
        root = new Uri(endpoint.AbsoluteUri.TrimEnd('/') + "/");
        client = new HttpClient(handler ?? new HttpClientHandler { AllowAutoRedirect = false }) { Timeout = TimeSpan.FromMinutes(2) };
        client.DefaultRequestHeaders.Authorization = new AuthenticationHeaderValue("Basic", Convert.ToBase64String(Encoding.UTF8.GetBytes(username + ":" + password)));
    }
    Uri Address(string relative)
    {
        if (relative.StartsWith('/') || relative.Contains("..") || relative.Contains('\\') || relative.Contains(':')) throw new InvalidDataException("WebDAV路径无效。");
        return new Uri(root, string.Join('/', relative.Split('/').Select(Uri.EscapeDataString)));
    }
    static async Task RequireSuccess(HttpResponseMessage response)
    {
        if (!response.IsSuccessStatusCode) throw new IOException("WebDAV请求失败，HTTP " + (int)response.StatusCode + "。");
        await Task.CompletedTask;
    }
    public async Task<IReadOnlyList<string>> Directories(string path, CancellationToken ct)
    {
        var address = Address(path.TrimEnd('/') + "/");
        using var request = new HttpRequestMessage(new HttpMethod("PROPFIND"), address);
        request.Headers.Add("Depth", "1");
        request.Content = new StringContent("<d:propfind xmlns:d='DAV:'><d:prop><d:resourcetype/></d:prop></d:propfind>", Encoding.UTF8, "application/xml");
        using var response = await client.SendAsync(request, ct);
        if (response.StatusCode == HttpStatusCode.NotFound) return [];
        await RequireSuccess(response);
        using var stream = await response.Content.ReadAsStreamAsync(ct);
        using var reader = XmlReader.Create(stream, new XmlReaderSettings { DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null, MaxCharactersInDocument = 1024 * 1024 });
        var document = XDocument.Load(reader); XNamespace dav = "DAV:";
        var names = new List<string>();
        foreach (var node in document.Descendants(dav + "response"))
        {
            if (!node.Descendants(dav + "collection").Any()) continue;
            var href = node.Element(dav + "href")?.Value;
            if (href == null || !Uri.TryCreate(address, href, out var uri) || uri.Host != root.Host || uri.Port != root.Port || uri.Scheme != root.Scheme || !uri.AbsolutePath.StartsWith(address.AbsolutePath, StringComparison.Ordinal)) continue;
            string name = Uri.UnescapeDataString(uri.AbsolutePath[address.AbsolutePath.Length..]).TrimEnd('/');
            if (SkinStore.IsId(name)) names.Add(name);
        }
        return names.Distinct().ToArray();
    }
    public async Task<DavFile?> Get(string path, CancellationToken ct)
    {
        using var response = await client.GetAsync(Address(path), HttpCompletionOption.ResponseHeadersRead, ct);
        if (response.StatusCode == HttpStatusCode.NotFound) return null;
        await RequireSuccess(response);
        if (response.Content.Headers.ContentLength > 32L * 1024 * 1024) throw new InvalidDataException("远端文件过大。");
        using var input = await response.Content.ReadAsStreamAsync(ct); using var output = new MemoryStream();
        byte[] buffer = new byte[65536]; int count;
        while ((count = await input.ReadAsync(buffer, ct)) > 0) { if (output.Length + count > 32L * 1024 * 1024) throw new InvalidDataException("远端文件过大。"); await output.WriteAsync(buffer.AsMemory(0, count), ct); }
        return new(output.ToArray(), response.Headers.ETag?.ToString());
    }
    public async Task MakeDirectory(string path, CancellationToken ct)
    {
        using var request = new HttpRequestMessage(new HttpMethod("MKCOL"), Address(path.TrimEnd('/') + "/"));
        using var response = await client.SendAsync(request, ct);
        if (response.StatusCode != HttpStatusCode.MethodNotAllowed) await RequireSuccess(response);
    }
    public async Task Put(string path, byte[] content, string? expectedETag, bool createOnly, CancellationToken ct)
    {
        using var request = new HttpRequestMessage(HttpMethod.Put, Address(path)) { Content = new ByteArrayContent(content) };
        if (createOnly) request.Headers.IfNoneMatch.Add(EntityTagHeaderValue.Any);
        else if (expectedETag != null) request.Headers.IfMatch.Add(EntityTagHeaderValue.Parse(expectedETag));
        else throw new IOException("服务器未提供ETag，无法安全更新同步指针。");
        using var response = await client.SendAsync(request, ct);
        if (response.StatusCode == HttpStatusCode.PreconditionFailed) throw new IOException("同步冲突：本设备目录被其他客户端更新，请核对installation_id后重试。");
        await RequireSuccess(response);
    }
    public void Dispose() => client.Dispose();
}
public sealed class SyncIndex
{
    public int SchemaVersion { get; set; } = 1;
    public string Generation { get; set; } = "";
    public Dictionary<string, string> Files { get; set; } = [];
}
public sealed record SyncResult(string BackupDirectory, int Downloaded, int Uploaded, string IncomingConfigDirectory);
public sealed class SnapshotSync(IWebDav dav, IRimeRunner rime)
{
    public async Task<SyncResult> Run(string rimeRoot, CancellationToken ct = default)
    {
        string root = Path.GetFullPath(rimeRoot), localId = RimeSnapshots.InstallationId(root);
        using var syncLock = new FileStream(Path.Combine(root, ".shorekeeper-sync.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        string backup = RimeSnapshots.BackupAll(root);
        string incoming = Path.Combine(root, "backups", "incoming-config");
        string? previousETag = null; bool previousExists = false; int downloaded = 0;
        var staged = new List<(string Path, byte[] Bytes)>(); long downloadedBytes = 0;
        var devices = await dav.Directories("rime-sync", ct);
        if (devices.Count > 32) throw new InvalidDataException("远端设备数量超过32。");
        foreach (string id in devices.Distinct())
        {
            if (!SkinStore.IsId(id)) throw new InvalidDataException("远端设备标识无效。");
            var current = await dav.Get($"rime-sync/{id}/latest.json", ct); if (current == null) continue;
            if (id == localId) { previousETag = current.ETag; previousExists = true; }
            if (current.Content.Length > 1024 * 1024) throw new InvalidDataException("远端清单过大。");
            var index = JsonSerializer.Deserialize<SyncIndex>(current.Content, SkinJson.Options) ?? throw new InvalidDataException("远端清单无效。");
            if (index.SchemaVersion != 1 || !SkinStore.IsId(index.Generation) || index.Files.Count > 128) throw new InvalidDataException("远端快照版本或文件数无效。");
            foreach (var file in index.Files)
            {
                bool config = file.Key.StartsWith("config/"); string name = config ? file.Key[7..] : file.Key;
                if (name.Contains('/') || name.Contains('\\') || name.Contains(':') || name.Contains("..") ||
                    (!config && !name.EndsWith(".userdb.txt")) || (config && !IsSharedConfig(name))) throw new InvalidDataException("远端快照文件名无效。");
                var data = await dav.Get($"rime-sync/{id}/{index.Generation}/{file.Key}", ct) ?? throw new IOException("远端快照未完整提交。");
                downloadedBytes += data.Content.Length;
                if (downloadedBytes > SkinStore.MaxExpandedBytes) throw new InvalidDataException("本次下载超过100MiB。");
                if (!Convert.ToHexString(SHA256.HashData(data.Content)).Equals(file.Value, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("快照哈希不匹配。");
                string target = config ? Path.Combine(incoming, id, index.Generation, name) : Path.Combine(root, "sync", id, name);
                staged.Add((target, data.Content));
            }
        }
        // No file is changed until every remote file has been downloaded and checked.
        foreach (var file in staged)
        {
            ct.ThrowIfCancellationRequested(); Directory.CreateDirectory(Path.GetDirectoryName(file.Path)!);
            string temp = file.Path + "." + Guid.NewGuid().ToString("N") + ".tmp";
            try { await File.WriteAllBytesAsync(temp, file.Bytes, ct); File.Move(temp, file.Path, true); downloaded++; }
            finally { if (File.Exists(temp)) File.Delete(temp); }
        }
        await rime.RunAsync("sync", ct);
        ct.ThrowIfCancellationRequested();
        string own = Path.Combine(root, "sync", localId);
        if (!Directory.Exists(own)) throw new IOException("Rime未生成本机同步目录。");
        var uploads = Directory.GetFiles(own, "*.userdb.txt").ToDictionary(p => Path.GetFileName(p), File.ReadAllBytes);
        if (uploads.Count == 0) throw new IOException("Rime未生成用户词典快照。");
        foreach (string path in Directory.GetFiles(root)) if (IsSharedConfig(Path.GetFileName(path))) uploads.Add("config/" + Path.GetFileName(path), await File.ReadAllBytesAsync(path, ct));
        if (uploads.Count > 128 || uploads.Values.Any(b => b.Length > 32L * 1024 * 1024) || uploads.Values.Sum(b => (long)b.Length) > SkinStore.MaxExpandedBytes) throw new InvalidDataException("本机快照超过同步规模限制。");
        string generation = DateTime.UtcNow.ToString("yyyyMMddHHmmss") + "-" + Guid.NewGuid().ToString("N");
        await dav.MakeDirectory("rime-sync", ct); await dav.MakeDirectory($"rime-sync/{localId}", ct);
        await dav.MakeDirectory($"rime-sync/{localId}/{generation}", ct); await dav.MakeDirectory($"rime-sync/{localId}/{generation}/config", ct);
        var next = new SyncIndex { Generation = generation };
        foreach (var file in uploads)
        {
            await dav.Put($"rime-sync/{localId}/{generation}/{file.Key}", file.Value, null, true, ct);
            next.Files.Add(file.Key, Convert.ToHexString(SHA256.HashData(file.Value)));
        }
        // Publish the immutable generation only after all uploads succeed.
        await dav.Put($"rime-sync/{localId}/latest.json", JsonSerializer.SerializeToUtf8Bytes(next, SkinJson.Options), previousETag, !previousExists, ct);
        return new(backup, downloaded, uploads.Count, incoming);
    }
    static bool IsSharedConfig(string name) => System.Text.RegularExpressions.Regex.IsMatch(name, "^((wanxiang|shortcuts)_[a-z0-9_]+\\.(schema|custom)\\.yaml|[a-z0-9_]+\\.dict\\.yaml|shortcuts_[a-z0-9_]+\\.txt)$");
}
