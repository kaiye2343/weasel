using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.Win32.SafeHandles;

namespace Shorekeeper.Core;

public static class SkinDiagnostics
{
    static readonly Regex LogName = new("^(render|presentation)-[0-9]+\\.log(\\.previous)?$", RegexOptions.CultureInvariant);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern SafeProcessHandle OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern int GetPackageFamilyName(SafeProcessHandle process, ref uint count, StringBuilder? name);
    [DllImport("shell32.dll")]
    static extern int SHGetKnownFolderPath(in Guid folder, uint flags, nint token, out nint path);

    public static IReadOnlyList<string> PackageLogDirectories(string local, string family)
    {
        if (string.IsNullOrWhiteSpace(family) || family.IndexOfAny(['/', '\\', ':']) >= 0 || family is "." or "..") return [];
        string package = Path.Combine(local, "Packages", family);
        return [Path.Combine(package, "AC", "ShorekeeperWeasel", "render-logs"),
            Path.Combine(package, "LocalCache", "Local", "ShorekeeperWeasel", "render-logs"),
            Path.Combine(package, "LocalState", "ShorekeeperWeasel", "render-logs")];
    }

    public static void Collect(string storage, string destination)
    {
        var roots = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { Path.Combine(storage, "render-logs") };
        var processes = new List<object>();
        string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        if (OperatingSystem.IsWindows())
        {
            Guid folder = new("F1B32785-6FBA-4FCF-9D55-7B8E7F157091");
            if (SHGetKnownFolderPath(in folder, 0x10000, 0, out nint path) >= 0)
                try { local = Marshal.PtrToStringUni(path) ?? local; } finally { Marshal.FreeCoTaskMem(path); }
        }
        var ids = new HashSet<int>();
        foreach (string name in new[] { "WeaselServer", "SearchHost", "StartMenuExperienceHost", "TextInputHost", "ShellExperienceHost" })
            foreach (var process in Process.GetProcessesByName(name))
                using (process) ids.Add(process.Id);
        string desktopLogs = Path.Combine(storage, "render-logs");
        if (Directory.Exists(desktopLogs))
            foreach (string file in Directory.EnumerateFiles(desktopLogs).Take(128))
                if (LogName.IsMatch(Path.GetFileName(file)) && int.TryParse(Path.GetFileName(file).Split('-')[1].Split('.')[0], out int pid)) ids.Add(pid);
        foreach (int pid in ids.Take(64))
        {
            try
            {
                using var process = Process.GetProcessById(pid);
                string? family = null;
                int packageResult = 0;
                if (OperatingSystem.IsWindows())
                {
                    using var handle = OpenProcess(0x1000, false, pid);
                    if (handle.IsInvalid) packageResult = Marshal.GetLastWin32Error();
                    else
                    {
                        uint length = 0;
                        packageResult = GetPackageFamilyName(handle, ref length, null);
                        if (packageResult == 122 && length is > 0 and < 4096)
                        {
                            var value = new StringBuilder((int)length);
                            packageResult = GetPackageFamilyName(handle, ref length, value);
                            if (packageResult == 0) family = value.ToString();
                        }
                    }
                }
                if (family != null) foreach (string root in PackageLogDirectories(local, family)) roots.Add(root);
                processes.Add(new { Pid = pid, Name = process.ProcessName, Started = process.StartTime, Package = family, PackageResult = packageResult });
            }
            catch (Exception ex) when (ex is ArgumentException or InvalidOperationException or System.ComponentModel.Win32Exception)
            { processes.Add(new { Pid = pid, Access = "unavailable" }); }
        }
        SkinJson.WriteAtomic(Path.Combine(destination, "skin-hosts.json"), processes);
        CopyRenderLogs(roots, Path.Combine(destination, "skin-render-logs"));
    }

    public static void CopyRenderLogs(IEnumerable<string> roots, string destination)
    {
        Directory.CreateDirectory(destination);
        var records = new List<object>();
        int count = 0;
        foreach (string root in roots.Distinct(StringComparer.OrdinalIgnoreCase))
        {
            try
            {
                if (!Directory.Exists(root)) continue;
                foreach (var file in new DirectoryInfo(root).EnumerateFiles().Where(f => LogName.IsMatch(f.Name))
                    .OrderByDescending(f => f.LastWriteTimeUtc).Take(12))
                {
                    if (count >= 64) break;
                    if (file.Length > 512 * 1024 || (file.Attributes & FileAttributes.ReparsePoint) != 0) continue;
                    string name = $"{count++:D2}-{file.Name}";
                    using var input = new FileStream(file.FullName, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                    using var output = File.Create(Path.Combine(destination, name));
                    // Bound the copy even if a host appends while it is collected.
                    byte[] buffer = new byte[8192]; int remaining = 512 * 1024, read;
                    while (remaining > 0 && (read = input.Read(buffer, 0, Math.Min(remaining, buffer.Length))) > 0)
                    { output.Write(buffer, 0, read); remaining -= read; }
                    records.Add(new { Source = root, File = file.Name, Copy = name });
                }
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
            { records.Add(new { Source = root, Access = "unavailable" }); }
        }
        SkinJson.WriteAtomic(Path.Combine(destination, "sources.json"), records);
    }
}
