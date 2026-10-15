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
        Set(store.Read()); Remember();
        foreach (var check in new[] { enabled, followFont }) { check.Checked += (_, _) => Changed(); check.Unchecked += (_, _) => Changed(); }
        foreach (var combo in new[] { position, empty, density }) combo.SelectionChanged += (_, _) => Changed();
        font.TextChanged += (_, _) => Changed(); comment.TextChanged += (_, _) => Changed();
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
    }
    DisplayPreferences Edited()
    {
        if (!int.TryParse(font.Text, out int points) || !int.TryParse(comment.Text, out int comments)) throw new InvalidDataException("字号必须是整数。");
        return new() { AnnotationEnabled = enabled.IsChecked == true, AnnotationPosition = new[] { "above", "beside", "skin" }[position.SelectedIndex],
            EmptyAnnotationMode = empty.SelectedIndex == 0 ? "collapse" : "reserve", FontMode = followFont.IsChecked == true ? "skin" : "user",
            FontPoint = points, CommentFontPoint = comments, Density = new[] { "compact", "standard", "skin" }[density.SelectedIndex] };
    }
    void Remember() { initial = JsonSerializer.Serialize(Edited()); original = File.Exists(store.PathName) ? File.ReadAllBytes(store.PathName) : null; }
    public bool IsDirty { get { try { return initial != JsonSerializer.Serialize(Edited()); } catch { return true; } } }
}
