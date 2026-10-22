using System.IO;
using System.Text.Json;
using System.Windows.Controls;
using Shorekeeper.Core;

namespace Shorekeeper.Manager;

public sealed class DisplaySettingsPanel : StackPanel
{
    readonly ManagerContext context;
    readonly DisplayPreferencesStore store;
    readonly CheckBox enabled = new() { Content = "显示候选注音" };
    readonly ComboBox position = new() { ItemsSource = new[] { "上方", "旁边", "跟随皮肤" } };
    readonly ComboBox empty = new() { ItemsSource = new[] { "整页无注音时自动收起", "始终保持高度" } };
    readonly CheckBox followFont = new() { Content = "跟随皮肤字号" };
    readonly TextBox font = Elements.Editor(), comment = Elements.Editor();
    readonly ComboBox density = new() { ItemsSource = new[] { "紧凑", "标准", "跟随皮肤（兼容）" } };
    readonly ComboBox placement = new() { ItemsSource = new[] { "自动（优先下方）", "优先下方", "优先上方" } };
    readonly TextBox offsetX = Elements.Editor(), gap = Elements.Editor();
    readonly CheckBox avoid = new() { Content = "图片装饰也避让正在输入的文字" };
    string initial = "";
    byte[]? original;
    public DisplaySettingsPanel(ManagerContext context)
    {
        this.context = context; store = new(context.Skins.Root);
        Children.Add(Elements.Text("候选显示偏好", 18));
        Children.Add(Elements.Text("作用范围：全局 · 外观；来源：display.json。保存只刷新候选窗，不部署输入方案。", 12, true));
        Children.Add(enabled); AddEditor("注音位置", position); AddEditor("无注音时", empty);
        Children.Add(followFont); AddEditor("候选字及序号字号（pt）", font); AddEditor("拼音字号（pt）", comment); AddEditor("文字区显示密度", density);
        Children.Add(Elements.Text("紧凑密度只调整文字间距和内部留白，图片尺寸及装饰预留区域仍由皮肤控制。缺少可靠注音时留空；万象长词注音在“万象输入”中设置。", 12, true));
        Children.Add(Elements.Text("候选窗位置", 18));
        AddEditor("出现位置（空间不足时自动换边）", placement);
        AddEditor("水平偏移（−256～256，正数向右）", offsetX);
        AddEditor("与输入行的最小间距（0～128）", gap);
        Children.Add(avoid);
        Children.Add(Elements.Text("单位为 96 DPI 逻辑像素。图片只有与输入行横向相交时才增加避让。屏幕边缘会限制偏移；窗口大于可用区域时可能无法全部避让。仅作用于原生横排候选窗，下次候选窗显示时生效。", 12, true));
        Children.Add(Elements.Button("预览位置（使用未保存参数）", async () => await context.Run(this, async () =>
        {
            var selected = context.Skins.Selection();
            var skin = context.Skins.List().FirstOrDefault(s => s.Manifest.Id == selected.SkinId && s.Manifest.Version == selected.SkinVersion);
            var variant = skin?.Manifest.Variants.FirstOrDefault(v => v.Id == selected.VariantId);
            await NativePreviewLauncher.OpenAsync(context, skin, variant, Edited());
        })));
        Set(store.Read()); Remember();
        foreach (var check in new[] { enabled, followFont, avoid }) { check.Checked += (_, _) => Changed(); check.Unchecked += (_, _) => Changed(); }
        foreach (var combo in new[] { position, empty, density, placement }) combo.SelectionChanged += (_, _) => Changed();
        font.TextChanged += (_, _) => Changed(); comment.TextChanged += (_, _) => Changed();
        offsetX.TextChanged += (_, _) => Changed(); gap.TextChanged += (_, _) => Changed();
        Children.Add(Elements.Button("保存并刷新候选显示", async () => await context.Run(this, async () =>
        {
            byte[]? current = File.Exists(store.PathName) ? File.ReadAllBytes(store.PathName) : null;
            if ((current == null) != (original == null) || current != null && !current.SequenceEqual(original!)) throw new IOException("显示偏好已被其他程序修改，请重新载入。");
            bool applied = false;
            await store.SaveAndApplyAsync(Edited(), async () => { applied = await context.RefreshSkin(); });
            Remember(); context.MarkChanged(); context.MarkDisplaySaved(applied);
        }), true));
        Children.Add(Elements.Button("恢复默认显示参数", () => { Set(new()); Changed(); }));
    }
    void AddEditor(string label, Control editor) { Children.Add(Elements.Text(label, 14)); Children.Add(editor); }
    void Changed() { UpdateControls(); context.MarkChanged(); }
    void UpdateControls() { position.IsEnabled = empty.IsEnabled = enabled.IsChecked == true; font.IsEnabled = comment.IsEnabled = followFont.IsChecked != true; }
    void Set(DisplayPreferences value)
    {
        enabled.IsChecked = value.AnnotationEnabled; position.SelectedIndex = Array.IndexOf(new[] { "above", "beside", "skin" }, value.AnnotationPosition);
        empty.SelectedIndex = value.EmptyAnnotationMode == "collapse" ? 0 : 1; followFont.IsChecked = value.FontMode == "skin";
        font.Text = value.FontPoint.ToString(); comment.Text = value.CommentFontPoint.ToString();
        density.SelectedIndex = Array.IndexOf(new[] { "compact", "standard", "skin" }, value.Density); UpdateControls();
        placement.SelectedIndex = Array.IndexOf(new[] { "auto", "below", "above" }, value.CandidatePlacement);
        offsetX.Text = value.CandidateOffsetX.ToString(); gap.Text = value.CandidateGap.ToString(); avoid.IsChecked = value.AvoidDecorations;
    }
    DisplayPreferences Edited()
    {
        if (!int.TryParse(font.Text, out int points) || !int.TryParse(comment.Text, out int comments)) throw new InvalidDataException("字号必须是整数。");
        if (!int.TryParse(offsetX.Text, out int horizontal) || !int.TryParse(gap.Text, out int distance)) throw new InvalidDataException("位置偏移及间距必须是整数。");
        return new() { AnnotationEnabled = enabled.IsChecked == true, AnnotationPosition = new[] { "above", "beside", "skin" }[position.SelectedIndex],
            EmptyAnnotationMode = empty.SelectedIndex == 0 ? "collapse" : "reserve", FontMode = followFont.IsChecked == true ? "skin" : "user",
            FontPoint = points, CommentFontPoint = comments, Density = new[] { "compact", "standard", "skin" }[density.SelectedIndex],
            CandidatePlacement = new[] { "auto", "below", "above" }[placement.SelectedIndex], CandidateOffsetX = horizontal,
            CandidateGap = distance, AvoidDecorations = avoid.IsChecked == true };
    }
    void Remember() { initial = JsonSerializer.Serialize(Edited()); original = File.Exists(store.PathName) ? File.ReadAllBytes(store.PathName) : null; }
    public bool IsDirty { get { try { return initial != JsonSerializer.Serialize(Edited()); } catch { return true; } } }
}
