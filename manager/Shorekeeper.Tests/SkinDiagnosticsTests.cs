using Shorekeeper.Core;

static class SkinDiagnosticsTests
{
    public static void Run(string temp, Action<bool, string> check)
    {
        string first = Path.Combine(temp, "desktop-logs"), second = Path.Combine(temp, "container-logs"), target = Path.Combine(temp, "copied-logs");
        Directory.CreateDirectory(first); Directory.CreateDirectory(second);
        File.WriteAllText(Path.Combine(first, "render-123.log"), "desktop");
        File.WriteAllText(Path.Combine(second, "render-123.log"), "container");
        File.WriteAllText(Path.Combine(second, "presentation-123.log.previous"), "previous");
        File.WriteAllText(Path.Combine(second, "render-secret.log"), "not a renderer log");
        File.WriteAllBytes(Path.Combine(second, "render-124.log"), new byte[512 * 1024 + 1]);
        SkinDiagnostics.CopyRenderLogs([first, second, first], target);
        var logs = Directory.GetFiles(target, "*.log*");
        check(logs.Length == 3, "diagnostics collect desktop and container logs without filename collisions");
        check(logs.Select(File.ReadAllText).ToHashSet().SetEquals(["desktop", "container", "previous"]), "diagnostics retain rotations and reject unknown or oversized logs");
        var roots = SkinDiagnostics.PackageLogDirectories(temp, "Example.Search_123");
        check(roots.Count == 3 && roots[0].Contains(Path.Combine("Packages", "Example.Search_123", "AC")), "diagnostics resolve package paths without hardcoded search package");
        check(SkinDiagnostics.PackageLogDirectories(temp, "../escape").Count == 0, "diagnostics reject unsafe package identifiers");
    }
}
