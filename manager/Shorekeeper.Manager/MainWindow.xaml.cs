using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Shorekeeper.Core;

namespace Shorekeeper.Manager;
public partial class MainWindow : Window
{
    readonly ManagerContext context;
    readonly SettingsPageRegistry pages = new();
    bool loading;
    int activeIndex;
    UserControl? activePage;
    sealed record Scope(string? Id, string Name);
    public MainWindow()
    {
        InitializeComponent(); context = new();
        var factories = new Dictionary<string, Func<UserControl>>
        {
            ["general"] = () => new SettingsPage(context, "general"), ["schemas"] = () => new SchemaPage(context),
            ["wanxiang"] = () => new WanxiangPage(context), ["keys"] = () => new SettingsPage(context, "keys"),
            ["dictionary"] = () => new SettingsPage(context, "dictionary"), ["skins"] = () => new SkinPage(context),
            ["scenes"] = () => new ScenePage(context), ["backup"] = () => new BackupPage(context),
            ["sync"] = () => new SyncPage(context), ["diagnostics"] = () => new DiagnosticsPage(context)
        };
        foreach (var module in SettingsModules.All) pages.Register(module, factories[module.Id]);
        context.Status += text => StatusText.Text = text;
        context.Changed += () => UpdateState(); context.ConfigurationSaved += () => SaveState.Text = "已保存待部署";
        context.DisplaySaved += applied => SaveState.Text = applied ? "已应用" : "外观已保存待刷新";
        RimeDirectory.Text = context.Root; Navigation.ItemsSource = pages.Modules;
        Loaded += async (_, _) =>
        {
            Reload(); var args = Environment.GetCommandLineArgs();
            if (args.Contains("--ui-smoke"))
            {
                try
                {
                    foreach (var scope in ScopeSelector.Items.Cast<Scope>().ToList())
                    {
                        ScopeSelector.SelectedItem = scope;
                        for (int i = 0; i < pages.Modules.Count; i++) { Navigation.SelectedIndex = i; UpdateLayout(); if (activePage == null) throw new IOException(pages.Modules[i].Name + "：" + StatusText.Text); }
                    }
                    ScopeSelector.SelectedIndex = 0; Navigation.SelectedIndex = 0;
                }
                catch (Exception e) { Directory.CreateDirectory(context.Skins.Root); File.WriteAllText(Path.Combine(context.Skins.Root, "ui-smoke-error.txt"), e.ToString()); Application.Current.Shutdown(1); return; }
            }
            int capture = Array.IndexOf(args, "--capture-image");
            if (capture >= 0 && capture + 1 < args.Length)
                await Dispatcher.InvokeAsync(() =>
                {
                    UpdateLayout(); var surface = (FrameworkElement)Content;
                    var bitmap = new RenderTargetBitmap((int)surface.ActualWidth + 40, (int)surface.ActualHeight + 40, 96, 96, PixelFormats.Pbgra32);
                    var background = new DrawingVisual(); using (var drawing = background.RenderOpen()) drawing.DrawRectangle(Background, null, new Rect(0, 0, bitmap.PixelWidth, bitmap.PixelHeight)); bitmap.Render(background); bitmap.Render(this); var png = new PngBitmapEncoder(); png.Frames.Add(BitmapFrame.Create(bitmap));
                    string file = Path.GetFullPath(args[capture + 1]); Directory.CreateDirectory(Path.GetDirectoryName(file)!); using var stream = File.Create(file); png.Save(stream);
                }, DispatcherPriority.ApplicationIdle);
            if (args.Contains("--ui-smoke")) Application.Current.Shutdown();
        };
        Closing += (_, e) => { if (!CanDiscard()) e.Cancel = true; };
    }
    bool CanDiscard() => activePage is not ISettingsPage page || !page.IsDirty || MessageBox.Show("有尚未保存的修改。放弃修改并继续？", "尚未保存", MessageBoxButton.OKCancel) == MessageBoxResult.OK;
    void UpdateState()
    {
        if (activePage is ISettingsPage page && page.IsDirty) SaveState.Text = "尚未保存";
        else if (SaveState.Text == "尚未保存") SaveState.Text = "已载入 · 未修改";
    }
    void Reload()
    {
        loading = true;
        try
        {
            string root = Path.GetFullPath(RimeDirectory.Text);
            if (!root.Equals(context.Root, StringComparison.OrdinalIgnoreCase)) SaveState.Text = "已载入 · 未修改";
            context.Root = root;
            var schemas = new RimeSettings(context.Root).Schemas(); var scopes = schemas.Select(s => new Scope(s.Id, s.Name + " · " + s.Id)).Prepend(new Scope(null, "全局默认")).ToList();
            ScopeSelector.ItemsSource = scopes; ScopeSelector.SelectedItem = scopes.FirstOrDefault(s => s.Id == context.Schema) ?? scopes[0]; context.Schema = ((Scope)ScopeSelector.SelectedItem).Id;
            if (Navigation.SelectedIndex < 0) Navigation.SelectedIndex = 0;
            BuildPage(); StatusText.Text = context.PreviewMode ? "独立预览模式 · 不连接日常输入法" : "已载入设置，尚未修改文件。";
        }
        catch (Exception e) { StatusText.Text = "载入失败：" + e.Message; }
        finally { loading = false; }
    }
    void LoadSettings(object sender, RoutedEventArgs e) { if (CanDiscard()) Reload(); }
    void Navigate(object sender, SelectionChangedEventArgs e)
    {
        if (loading || context == null) return;
        if (!CanDiscard()) { loading = true; Navigation.SelectedIndex = activeIndex; loading = false; return; }
        BuildPage();
    }
    void ChangeScope(object sender, SelectionChangedEventArgs e)
    {
        if (loading || context == null || ScopeSelector.SelectedItem is not Scope scope) return;
        if (!CanDiscard()) { loading = true; ScopeSelector.SelectedItem = ScopeSelector.Items.Cast<Scope>().First(s => s.Id == context.Schema); loading = false; return; }
        context.Schema = scope.Id; BuildPage();
    }
    void BuildPage()
    {
        if (Navigation.SelectedItem is not SettingsModule module) return;
        TitleText.Text = module.Name; DescriptionText.Text = module.Description; activeIndex = Navigation.SelectedIndex;
        activePage = null; PageHost.Content = null;
        try { activePage = pages.Create(module.Id); PageHost.Content = activePage; }
        catch (Exception e) { StatusText.Text = "页面未载入：" + e.Message; PageHost.Content = Elements.Card(Elements.Text("无法读取设置，请检查配置目录及 YAML 格式。\n" + e.Message, 16)); }
        PreviewButton.IsEnabled = SaveButton.IsEnabled = activePage is ISettingsPage;
        if (SaveState.Text == "尚未保存") SaveState.Text = "已载入 · 未修改";
    }
    async void PreviewChanges(object sender, RoutedEventArgs e) => await context.Run(this, () =>
    {
        if (activePage is ISettingsPage page && page.Preview() is { } plan) ChangeReview.Confirm(plan); return Task.CompletedTask;
    });
    async void SaveSettings(object sender, RoutedEventArgs e) => await context.Run(this, async () =>
    {
        if (activePage is not ISettingsPage page || page.Preview() is not { } plan || !ChangeReview.Confirm(plan)) return;
        string backup = await Task.Run(() => plan.Save(context.Root)); page.Saved(); context.MarkSaved(); BuildPage(); StatusText.Text = "配置已保存待部署。备份：" + backup;
    });
    async void Deploy(object sender, RoutedEventArgs e) => await context.Run(this, async () =>
    {
        if (activePage is ISettingsPage page && page.IsDirty) throw new InvalidOperationException("请先保存当前修改，再部署应用。");
        await context.InstalledRunner().RunAsync("deploy"); SaveState.Text = "已应用"; StatusText.Text = "输入配置已由官方部署器应用。";
    });
}
