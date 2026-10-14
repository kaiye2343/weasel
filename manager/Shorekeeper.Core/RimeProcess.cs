using System.Diagnostics;
using System.IO.Compression;
using System.Security.Cryptography;
using YamlDotNet.RepresentationModel;

namespace Shorekeeper.Core;
public interface IRimeRunner { Task RunAsync(string operation, CancellationToken cancellation = default); }
public sealed class RimeOperationException(string operation, int exitCode) : IOException($"小狼毫操作未完成：{operation}，退出码{exitCode}。")
{
    public int ExitCode { get; } = exitCode;
}
public sealed class RimeProcess(string directory) : IRimeRunner
{
    public async Task RunAsync(string operation, CancellationToken cancellation = default)
    {
        if (operation is not ("sync" or "deploy" or "skin-reload" or "diagnostics")) throw new ArgumentException("Unknown Rime operation");
        string exe = Path.Combine(Path.GetFullPath(directory), operation is "skin-reload" or "diagnostics" ? "WeaselServer.exe" : "WeaselDeployer.exe");
        if (!File.Exists(exe)) throw new FileNotFoundException("未找到小狼毫组件。", exe);
        var start = new ProcessStartInfo(exe) { UseShellExecute = false, CreateNoWindow = true, WorkingDirectory = directory };
        start.ArgumentList.Add("/" + operation);
        using var process = Process.Start(start) ?? throw new IOException("无法启动小狼毫工具。");
        try { await process.WaitForExitAsync(cancellation); }
        catch (OperationCanceledException) { throw new IOException("等待已取消；请确认官方工具是否仍在运行，再重试。"); }
        if (process.ExitCode != 0) throw new RimeOperationException(operation, process.ExitCode);
    }
}
public static class RimeSnapshots
{
    public static void RequireDefaultSyncDirectory(string root)
    {
        var yaml = new YamlStream(); using var reader = File.OpenText(Path.Combine(root, "installation.yaml")); yaml.Load(reader);
        var map = (YamlMappingNode)yaml.Documents[0].RootNode;
        if (map.Children.TryGetValue("sync_dir", out var value) && value is YamlScalarNode scalar && !string.IsNullOrWhiteSpace(scalar.Value) &&
            !Path.GetFullPath(scalar.Value, root).TrimEnd(Path.DirectorySeparatorChar).Equals(Path.Combine(Path.GetFullPath(root), "sync"), StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("当前工具尚不支持自定义 sync_dir；请使用默认用户目录下的 sync 文件夹，避免读取错误快照。");
    }
    public static string InstallationId(string root)
    {
        var yaml = new YamlStream(); using var reader = File.OpenText(Path.Combine(root, "installation.yaml")); yaml.Load(reader);
        var map = (YamlMappingNode)yaml.Documents[0].RootNode;
        string id = ((YamlScalarNode)map.Children["installation_id"]).Value ?? "";
        if (!SkinStore.IsId(id)) throw new InvalidDataException("installation_id格式不受支持。"); return id;
    }
    public static string BackupAll(string root)
    {
        RequireDefaultSyncDirectory(root);
        string backup = Path.Combine(root, "backups", "shorekeeper-sync", DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fffffff"));
        Directory.CreateDirectory(backup);
        string sync = Path.Combine(root, "sync");
        if (Directory.Exists(sync)) foreach (string file in Directory.EnumerateFiles(sync, "*.userdb.txt", SearchOption.AllDirectories))
        {
            string dest = Path.Combine(backup, Path.GetRelativePath(sync, file)); Directory.CreateDirectory(Path.GetDirectoryName(dest)!); File.Copy(file, dest);
        }
        return backup;
    }
    public static async Task BackupProfileAsync(string root, string id, string destination, IRimeRunner runner)
    {
        RimeProfiles.ValidateId(id); if (File.Exists(destination)) throw new IOException("备份目标已存在。");
        RequireDefaultSyncDirectory(root);
        await runner.RunAsync("sync");
        using var zip = ZipFile.Open(destination, ZipArchiveMode.Create);
        foreach (string file in Directory.EnumerateFiles(root, "*", SearchOption.TopDirectoryOnly))
            if (Path.GetFileName(file) is var name && (name.StartsWith("wanxiang_" + id + ".") || name == id + ".dict.yaml" || name.StartsWith("shortcuts_" + id + "."))) zip.CreateEntryFromFile(file, "config/" + name);
        foreach (string file in Directory.EnumerateFiles(Path.Combine(root, "sync"), "*.userdb.txt", SearchOption.AllDirectories))
        {
            string name = Path.GetFileName(file);
            if (new[] { "wanxiang_", "en_", "zc_", "context_reorder_", "sequence_", "stats_" }.Any(prefix => name == prefix + id + ".userdb.txt"))
                zip.CreateEntryFromFile(file, "sync/" + Path.GetRelativePath(Path.Combine(root, "sync"), file).Replace('\\', '/'));
        }
    }
    public static async Task RestoreProfileAsync(string root, string archive, IRimeRunner runner)
    {
        if (new FileInfo(archive).Length > SkinStore.MaxArchiveBytes) throw new InvalidDataException("备份压缩包过大。");
        BackupAll(root); var config = new RimeProfiles(root);
        using var zip = ZipFile.OpenRead(archive);
        if (zip.Entries.Count > 256) throw new InvalidDataException("备份文件过多。");
        var staged = new List<(string Target, string Text)>(); long bytes = 0;
        foreach (var entry in zip.Entries)
        {
            bytes += entry.Length; if (bytes > SkinStore.MaxExpandedBytes) throw new InvalidDataException("备份过大。");
            string rel = entry.FullName;
            if (rel.StartsWith("config/")) rel = rel[7..];
            else if (!rel.StartsWith("sync/") || !rel.EndsWith(".userdb.txt")) throw new InvalidDataException("备份路径无效。");
            if (!rel.StartsWith("sync/") && (!RegexName(Path.GetFileName(rel)) || rel.Contains('/'))) throw new InvalidDataException("配置备份无效。");
            string target = SkinStore.ResolveAsset(root, rel);
            using var reader = new StreamReader(entry.Open()); staged.Add((target, await reader.ReadToEndAsync()));
        }
        config.WriteBatchBackedUp(staged.ToDictionary(f => f.Target, f => f.Text, StringComparer.OrdinalIgnoreCase));
        await runner.RunAsync("deploy"); await runner.RunAsync("sync");
    }
    static bool RegexName(string name) => System.Text.RegularExpressions.Regex.IsMatch(name, "^((wanxiang|shortcuts)_[a-z0-9_]+\\.(schema|custom)\\.yaml|[a-z0-9_]+\\.dict\\.yaml|shortcuts_[a-z0-9_]+\\.txt)$");
    public static string CollectDiagnostics(string root, string storage)
    {
        string directory = Path.Combine(storage, "diagnostics", DateTime.Now.ToString("yyyyMMdd-HHmmss-fffffff")); Directory.CreateDirectory(directory);
        var metadata = new List<object>();
        foreach (var process in Process.GetProcessesByName("WeaselServer"))
        {
            using (process) try { metadata.Add(new { process.Id, process.StartTime, Executable = process.MainModule?.FileName, Version = process.MainModule?.FileVersionInfo.FileVersion }); } catch { metadata.Add(new { process.Id, Access = "unavailable" }); }
        }
        SkinJson.WriteAtomic(Path.Combine(directory, "processes.json"), metadata);
        string runtime = Path.Combine(storage, "runtime.json");
        if (File.Exists(runtime)) File.Copy(runtime, Path.Combine(directory, "runtime.json"));
        var hashes = new Dictionary<string, string>();
        foreach (string rel in new[] { "installation.yaml", "user.yaml", "default.custom.yaml", "weasel.custom.yaml", "build/default.yaml", "build/weasel.yaml" })
        {
            string file = Path.Combine(root, rel); if (!File.Exists(file)) continue;
            File.Copy(file, Path.Combine(directory, rel.Replace('/', '_')));
            using var stream = File.OpenRead(file); hashes[rel] = Convert.ToHexString(SHA256.HashData(stream));
        }
        string logs = Path.Combine(Path.GetTempPath(), "rime.weasel");
        if (Directory.Exists(logs)) foreach (var file in new DirectoryInfo(logs).EnumerateFiles().OrderByDescending(f => f.LastWriteTimeUtc).Take(20))
            if (file.Length < 20 * 1024 * 1024) try { File.Copy(file.FullName, Path.Combine(directory, file.Name)); } catch (IOException) { }
        SkinJson.WriteAtomic(Path.Combine(directory, "config-hashes.json"), hashes);
        return directory;
    }
}
