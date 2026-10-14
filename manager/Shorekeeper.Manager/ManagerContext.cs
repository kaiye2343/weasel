using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;
using Shorekeeper.Core;

namespace Shorekeeper.Manager;

public sealed class ManagerContext
{
    public SkinStore Skins { get; }
    public string Root { get; set; }
    public string? Schema { get; set; }
    public bool PreviewMode { get; }
    public string Tools => AppContext.BaseDirectory;
    public event Action<string>? Status;
    public event Action? Changed;
    public event Action? ConfigurationSaved;
    public ManagerContext()
    {
        var args = Environment.GetCommandLineArgs();
        string? Arg(string key) { int n = Array.IndexOf(args, key); return n >= 0 && n + 1 < args.Length ? args[n + 1] : null; }
        Skins = new(Arg("--data-root")); Root = Arg("--rime-root") ?? InstalledUserDirectory(); PreviewMode = args.Contains("--preview-mode");
    }
    public void Report(string text) => Status?.Invoke(text);
    public void MarkChanged() => Changed?.Invoke();
    public void MarkSaved() => ConfigurationSaved?.Invoke();
    public static string InstalledUserDirectory()
    {
        using var key = Registry.CurrentUser.OpenSubKey(@"Software\Rime\Weasel");
        return Path.GetFullPath(key?.GetValue("RimeUserDir") as string ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Rime"));
    }
    public RimeProcess InstalledRunner()
    {
        if (PreviewMode) throw new IOException("独立预览模式不会连接日常输入法。");
        if (!Path.GetFullPath(Root).TrimEnd('\\').Equals(InstalledUserDirectory().TrimEnd('\\'), StringComparison.OrdinalIgnoreCase)) throw new IOException("官方部署与同步只允许当前安装的用户目录；隔离目录仅保存和预览。");
        foreach (var location in new[] { @"SOFTWARE\WOW6432Node\Rime\Weasel", @"SOFTWARE\Rime\Weasel" })
        {
            using var key = Registry.LocalMachine.OpenSubKey(location);
            if (key?.GetValue("WeaselRoot") is string directory) return new(directory);
        }
        throw new IOException("未找到已安装的小狼毫。");
    }
    public async Task RefreshSkin()
    {
        if (PreviewMode) { Report("外观选择已保存到隔离目录；独立预览不连接日常输入法。"); return; }
        try { await InstalledRunner().RunAsync("skin-reload"); Report("外观已应用；未部署或重启 Rime。"); }
        catch (RimeOperationException e) when (e.ExitCode == 2) { Report("外观已保存；服务未运行，将在下次启动时加载。"); }
    }
    public async Task Run(FrameworkElement owner, Func<Task> action)
    {
        if (!owner.IsEnabled) return;
        owner.IsEnabled = false;
        try { await action(); } catch (Exception e) { Report("未完成：" + e.Message); }
        finally { owner.IsEnabled = true; }
    }
    public static void OpenDirectory(string path)
    {
        if (!Directory.Exists(path)) throw new DirectoryNotFoundException("目录不存在。");
        Process.Start(new ProcessStartInfo("explorer.exe") { UseShellExecute = true, ArgumentList = { Path.GetFullPath(path) } });
    }
}

public interface ISettingsPage
{
    bool IsDirty { get; }
    ConfigurationPlan? Preview();
    void Saved();
}
public static class Elements
{
    public static TextBlock Text(string text, int size = 14, bool muted = false) => new() { Text = text, FontSize = size, TextWrapping = TextWrapping.Wrap, Margin = new(0, 0, 0, 8), Foreground = (System.Windows.Media.Brush)Application.Current.FindResource(muted ? "Muted" : "Ink") };
    public static Border Card(UIElement child) => new() { Child = child, Background = System.Windows.Media.Brushes.White, CornerRadius = new(12), Padding = new(22), Margin = new(0, 0, 0, 16), BorderBrush = (System.Windows.Media.Brush)Application.Current.FindResource("Line"), BorderThickness = new(1) };
    public static Button Button(string label, Action action, bool accent = false)
    {
        var button = new Button { Content = label, HorizontalAlignment = HorizontalAlignment.Left };
        if (accent) button.Style = (Style)Application.Current.FindResource("AccentButton");
        button.Click += (_, _) => action(); return button;
    }
    public static ScrollViewer Scroll(UIElement child) => new() { Content = child, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled };
    public static TextBox Editor(string text = "", bool multiline = false) => new() { Text = text, AcceptsReturn = multiline, AcceptsTab = multiline, MinHeight = multiline ? 105 : 38, VerticalScrollBarVisibility = multiline ? ScrollBarVisibility.Auto : ScrollBarVisibility.Hidden };
}
