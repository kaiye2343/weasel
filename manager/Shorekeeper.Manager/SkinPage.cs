using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media.Imaging;
using Microsoft.Win32;
using Shorekeeper.Core;

namespace Shorekeeper.Manager;

public sealed class SkinPage : UserControl
{
    readonly ManagerContext context;
    readonly ListBox library = new() { MinHeight = 330, DisplayMemberPath = "Label" };
    readonly ComboBox variants = new() { DisplayMemberPath = "Name" };
    readonly Image cover = new() { Height = 220, Stretch = System.Windows.Media.Stretch.Uniform, Margin = new(0, 12, 0, 16) };
    readonly TextBlock title = Elements.Text("选择或导入皮肤", 24);
    readonly TextBlock info = Elements.Text("", 13, true);
    List<Entry> entries = [];
    sealed record Entry(InstalledSkin Skin) { public string Label => Skin.Manifest.Name + " · " + Skin.Manifest.Version; }
    Entry? Current => library.SelectedItem as Entry;
    SkinVariant? Variant => variants.SelectedItem as SkinVariant;
    public SkinPage(ManagerContext context)
    {
        this.context = context;
        var grid = new Grid(); grid.ColumnDefinitions.Add(new() { Width = new(260) }); grid.ColumnDefinitions.Add(new() { Width = new(20) }); grid.ColumnDefinitions.Add(new());
        var left = new StackPanel(); var search = Elements.Editor(); search.ToolTip = "按名称、作者、标识或版本搜索"; search.TextChanged += (_, _) => Filter(search.Text); left.Children.Add(Elements.Text("已安装皮肤", 19)); left.Children.Add(search); left.Children.Add(library);
        left.Children.Add(Elements.Button("导入皮肤包", Import)); left.Children.Add(Elements.Button("恢复原生默认外观", async () => await context.Run(this, async () =>
        {
            var previous = context.Skins.Selection(); context.Skins.Reset();
            try { await context.RefreshSkin(); } catch { SkinJson.WriteAtomic(context.Skins.StatePath, previous); throw; }
        })));
        grid.Children.Add(Elements.Card(left));
        var right = new StackPanel(); right.Children.Add(title); right.Children.Add(info); right.Children.Add(cover); right.Children.Add(Elements.Text("皮肤变体", 16)); right.Children.Add(variants);
        var actions = new WrapPanel(); actions.Children.Add(Elements.Button("原生预览", Preview)); actions.Children.Add(Elements.Button("启用此版本", Apply, true)); actions.Children.Add(Elements.Button("导出皮肤包", Export)); right.Children.Add(actions);
        right.Children.Add(Elements.Text("选择其他已安装版本再启用，即可回退。皮肤只影响候选窗外观，不改变方案、简繁或学习数据。", 13, true));
        right.Children.Add(Elements.Button("打开离线皮肤设计器", () =>
        {
            string page = Path.Combine(context.Tools, "Designer", "index.html");
            if (!File.Exists(page)) { context.Report("未找到网页设计器；请下载完整构建包中的 rime-skin-studio.zip。"); return; }
            try { Process.Start(new ProcessStartInfo(page) { UseShellExecute = true }); }
            catch (Exception e) when (e is IOException or System.ComponentModel.Win32Exception) { context.Report("无法打开设计器：" + e.Message); }
        }));
        right.Children.Add(Elements.Text("设计器在独立离线网页中编辑，导出后再导入此处。设置中心不编辑图片布局；仅支持无注音的皮肤会优先于全局注音偏好。", 13, true));
        var card = Elements.Card(right); Grid.SetColumn(card, 2); grid.Children.Add(card); Content = Elements.Scroll(grid);
        library.SelectionChanged += (_, _) => SelectEntry(); Loaded += (_, _) => Refresh();
    }
    void Refresh()
    {
        entries = context.Skins.List().OrderBy(s => s.Manifest.Name).ThenByDescending(s => Version.TryParse(s.Manifest.Version, out var v) ? v : new Version()).Select(s => new Entry(s)).ToList(); Filter("");
        var state = context.Skins.Selection(); library.SelectedItem = entries.FirstOrDefault(e => e.Skin.Manifest.Id == state.SkinId && e.Skin.Manifest.Version == state.SkinVersion) ?? entries.FirstOrDefault();
        if (state.VariantId != null) variants.SelectedItem = variants.Items.Cast<SkinVariant>().FirstOrDefault(v => v.Id == state.VariantId) ?? variants.SelectedItem;
        if (entries.Count == 0) { title.Text = "使用小狼毫原生默认外观"; info.Text = "尚未导入皮肤。输入法无需皮肤包也可以运行。"; }
    }
    void Filter(string value)
    {
        var selected = Current; library.ItemsSource = entries.Where(e => (e.Label + e.Skin.Manifest.Id + e.Skin.Manifest.Author).Contains(value, StringComparison.OrdinalIgnoreCase)).ToList(); library.SelectedItem = selected != null && library.Items.Contains(selected) ? selected : library.Items.Cast<Entry>().FirstOrDefault();
    }
    void SelectEntry()
    {
        if (Current == null) { variants.ItemsSource = null; cover.Source = null; return; }
        title.Text = Current.Label; info.Text = $"作者：{Current.Skin.Manifest.Author}    标识：{Current.Skin.Manifest.Id}\n变体数量：{Current.Skin.Manifest.Variants.Count}    来源：本地已导入皮肤包";
        variants.ItemsSource = Current.Skin.Manifest.Variants; variants.SelectedIndex = 0;
        try { var bitmap = new BitmapImage(); bitmap.BeginInit(); bitmap.CacheOption = BitmapCacheOption.OnLoad; bitmap.UriSource = new(SkinStore.ResolveAsset(Current.Skin.Directory, Current.Skin.Manifest.Preview)); bitmap.EndInit(); bitmap.Freeze(); cover.Source = bitmap; }
        catch (Exception e) when (e is IOException or NotSupportedException or ArgumentException) { cover.Source = null; context.Report("封面读取失败：" + e.Message); }
    }
    async void Import()
    {
        var dialog = new OpenFileDialog { Filter = "皮肤包|*.rimeskin;*.zip" }; if (dialog.ShowDialog() != true) return;
        await context.Run(this, async () => { var skin = await Task.Run(() => context.Skins.Import(dialog.FileName)); Refresh(); library.SelectedItem = entries.First(e => e.Skin.Directory == skin.Directory); context.Report("皮肤已导入，尚未启用；可以先打开原生预览。"); });
    }
    async void Apply() => await context.Run(this, async () =>
    {
        if (Current == null || Variant == null) throw new InvalidOperationException("请选择皮肤及变体。");
        var previous = context.Skins.Selection();
        // Preserve legacy overrides only when reselecting their exact package and variant.
        var legacy = previous.SkinId == Current.Skin.Manifest.Id && previous.SkinVersion == Current.Skin.Manifest.Version && previous.VariantId == Variant.Id ? previous.StyleOverride : null;
        context.Skins.Select(Current.Skin, Variant.Id, legacy);
        try { await context.RefreshSkin(); } catch { SkinJson.WriteAtomic(context.Skins.StatePath, previous); throw; }
    });
    async void Preview() => await context.Run(this, async () =>
    {
        string exe = Path.Combine(context.Tools, "WeaselSkinPreview.exe"); if (!File.Exists(exe)) throw new FileNotFoundException("完整构建包中提供原生预览程序。");
        var start = new ProcessStartInfo(exe) { UseShellExecute = false, WorkingDirectory = context.Tools };
        string? temporary = null;
        start.ArgumentList.Add("--display-preferences");
        start.ArgumentList.Add(new DisplayPreferencesStore(context.Skins.Root).PathName);
        if (Current != null && Variant != null)
        {
            temporary = Path.Combine(context.Skins.Root, "previews", Guid.NewGuid().ToString("N") + ".json");
            var style = new DisplayPreferencesStore(context.Skins.Root).EffectiveStyle(context.Skins, Current.Skin, Variant);
            SkinJson.WriteAtomic(temporary, style);
            foreach (var arg in new[] { "--manifest", Path.Combine(Current.Skin.Directory, "manifest.json"), "--variant", Variant.Id, "--style-overrides", temporary }) start.ArgumentList.Add(arg);
        }
        using var process = Process.Start(start) ?? throw new IOException("无法打开预览。");
        context.Report("已打开原生隔离预览；不连接日常输入法。");
        if (temporary != null) { string file = temporary; _ = Cleanup(process.Id, file); }
        await Task.CompletedTask;
    });
    static async Task Cleanup(int pid, string file)
    {
        try { using var process = Process.GetProcessById(pid); await process.WaitForExitAsync(); } catch (ArgumentException) { }
        try { File.Delete(file); } catch (IOException) { }
    }
    async void Export()
    {
        if (Current == null) return; var skin = Current.Skin;
        var dialog = new SaveFileDialog { Filter = "皮肤包|*.rimeskin", FileName = skin.Manifest.Id + "-" + skin.Manifest.Version + ".rimeskin" }; if (dialog.ShowDialog() != true) return;
        await context.Run(this, () => { context.Skins.Export(skin, dialog.FileName); context.Report("已导出原始皮肤包；全局显示偏好不写入皮肤。"); return Task.CompletedTask; });
    }
}
