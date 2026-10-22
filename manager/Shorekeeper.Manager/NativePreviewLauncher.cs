using System.Diagnostics;
using System.IO;
using Shorekeeper.Core;

namespace Shorekeeper.Manager;

public static class NativePreviewLauncher
{
    public static async Task OpenAsync(ManagerContext context, InstalledSkin? skin, SkinVariant? variant, DisplayPreferences? preferences = null)
    {
        string exe = Path.Combine(context.Tools, "WeaselSkinPreview.exe");
        if (!File.Exists(exe)) throw new FileNotFoundException("完整构建包中提供原生预览程序。");
        var files = new List<string>();
        try
        {
            string NewFile() => Path.Combine(context.Skins.Root, "previews", Guid.NewGuid().ToString("N") + ".json");
            var start = new ProcessStartInfo(exe) { UseShellExecute = false, WorkingDirectory = context.Tools };
            string display = new DisplayPreferencesStore(context.Skins.Root).PathName;
            if (preferences != null)
            {
                display = NewFile(); files.Add(display);
                DisplayPreferencesStore.Validate(preferences);
                SkinJson.WriteAtomic(display, preferences);
            }
            start.ArgumentList.Add("--display-preferences"); start.ArgumentList.Add(display);
            if (skin != null && variant != null)
            {
                string style = NewFile(); files.Add(style);
                var selected = context.Skins.Selection();
                var effective = selected.SkinId == skin.Manifest.Id && selected.SkinVersion == skin.Manifest.Version && selected.VariantId == variant.Id
                    ? selected.StyleOverride ?? variant.Style : variant.Style;
                SkinJson.WriteAtomic(style, effective);
                foreach (var arg in new[] { "--manifest", Path.Combine(skin.Directory, "manifest.json"), "--variant", variant.Id, "--style-overrides", style }) start.ArgumentList.Add(arg);
            }
            var process = Process.Start(start) ?? throw new IOException("无法打开预览。");
            context.Report("已打开带模拟输入行的原生预览；未保存参数不会改动日常外观。");
            _ = CleanupAsync(process, files.ToArray()); files.Clear();
        }
        finally { foreach (var file in files) TryDelete(file); }
        await Task.CompletedTask;
    }
    static async Task CleanupAsync(Process process, string[] files)
    {
        try { await process.WaitForExitAsync(); }
        finally { process.Dispose(); foreach (var file in files) TryDelete(file); }
    }
    static void TryDelete(string path) { try { File.Delete(path); } catch (IOException) { } catch (UnauthorizedAccessException) { } }
}
