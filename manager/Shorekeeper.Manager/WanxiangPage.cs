using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using Shorekeeper.Core;

namespace Shorekeeper.Manager;

public sealed class WanxiangPage : UserControl, ISettingsPage
{
    readonly ManagerContext context;
    readonly string? schema;
    readonly ComboBox mode = new();
    readonly CheckBox simplified = new() { Content = "默认简体" }, correction = new() { Content = "拼写纠错" }, reorder = new() { Content = "上下文排序" };
    readonly List<CheckBox> fuzzy = [];
    readonly TextBox shortcuts = Elements.Editor("", true);
    InputSettings? original;
    string originalCustom = "";
    string customPath = "";
    string shortcutsPath = "", originalShortcuts = "";
    public WanxiangPage(ManagerContext context)
    {
        this.context = context; schema = context.Schema;
        var panel = new StackPanel();
        if (schema == null || schema != "wanxiang" && !schema.StartsWith("wanxiang_"))
        {
            panel.Children.Add(Elements.Card(Elements.Text("请选择万象输入方案后载入。普通 Rime 方案使用通用设置，不会写入万象专属补丁。", 17))); Content = panel; return;
        }
        var body = new StackPanel(); body.Children.Add(Elements.Text("拼音与学习", 20));
        try { original = new RimeProfiles(context.Root).LoadScheme(schema); }
        catch (Exception e) when (e is IOException or InvalidDataException) { body.Children.Add(Elements.Text("此万象版本的开关结构暂不支持：" + e.Message, 14, true)); panel.Children.Add(Elements.Card(body)); Content = panel; return; }
        customPath = Path.Combine(context.Root, schema + ".custom.yaml"); originalCustom = File.Exists(customPath) ? File.ReadAllText(customPath) : "";
        if (schema.StartsWith("wanxiang_")) { shortcutsPath = Path.Combine(context.Root, "shortcuts_" + schema[9..] + ".dict.yaml"); originalShortcuts = File.Exists(shortcutsPath) ? File.ReadAllText(shortcutsPath) : ""; }
        body.Children.Add(Elements.Text("作用范围：" + schema + "    来源：" + schema + ".schema.yaml / " + schema + ".custom.yaml", 12, true));
        mode.ItemsSource = new[] { "全拼", "自然码", "小鹤双拼", "微软双拼", "搜狗双拼", "智能ABC", "紫光双拼", "拼音加加" }; mode.SelectedItem = original.PinyinMode; body.Children.Add(mode);
        simplified.IsChecked = original.Simplified; correction.IsChecked = original.EnableCorrection; reorder.IsChecked = original.ContextReorder;
        foreach (var check in new[] { simplified, correction, reorder }) { check.Margin = new(0, 8, 0, 10); body.Children.Add(check); check.Checked += (_, _) => context.MarkChanged(); check.Unchecked += (_, _) => context.MarkChanged(); }
        mode.SelectionChanged += (_, _) => context.MarkChanged(); panel.Children.Add(Elements.Card(body));
        var f = new StackPanel(); f.Children.Add(Elements.Text("模糊音", 20)); f.Children.Add(Elements.Text("按需开启，避免不相关候选过多。保留不属于此模块的自定义拼写规则。", 12, true));
        var groups = new WrapPanel();
        foreach (string name in RimeProfiles.FuzzyNames) { var check = new CheckBox { Content = name.Replace('_', '/'), Tag = name, IsChecked = original.FuzzyGroups.Contains(name), Margin = new(0, 8, 28, 12) }; check.Checked += (_, _) => context.MarkChanged(); check.Unchecked += (_, _) => context.MarkChanged(); fuzzy.Add(check); groups.Children.Add(check); }
        f.Children.Add(groups); panel.Children.Add(Elements.Card(f));
        var p = new StackPanel(); p.Children.Add(Elements.Text("快捷短语", 20));
        if (schema.StartsWith("wanxiang_")) { shortcuts.Text = string.Join('\n', original.Shortcuts.Select(p => p.Key + "\t" + p.Value)); shortcuts.TextChanged += (_, _) => context.MarkChanged(); p.Children.Add(shortcuts); p.Children.Add(Elements.Text("每行：缩写<Tab>短语。场景短语独立保存。", 12, true)); }
        else p.Children.Add(Elements.Text("基础方案的已有短语保持原样。可新建独立学习场景，在该场景中管理快捷短语。", 13, true));
        panel.Children.Add(Elements.Card(p));
        var capabilities = new StackPanel(); capabilities.Children.Add(Elements.Text("复用万象能力", 20));
        capabilities.Children.Add(Elements.Text("计算、日期、金额、符号等由已安装的万象 Lua 组件提供；设置中心不重复实现或猜测不同版本的触发命令。", 13, true));
        var doc = new RimeSettings(context.Root).EffectiveSchema(schema);
        if (doc.Children.TryGetValue("engine", out var engine)) capabilities.Children.Add(Elements.Text("当前方案组件：\n" + engine, 12, true));
        panel.Children.Add(Elements.Card(capabilities)); Content = Elements.Scroll(panel);
    }
    InputSettings Edited()
    {
        if (original == null) throw new InvalidOperationException("当前方案不支持万象设置。");
        var value = new InputSettings { PinyinMode = mode.SelectedItem as string ?? original.PinyinMode, Simplified = simplified.IsChecked == true, EnableCorrection = correction.IsChecked == true, ContextReorder = reorder.IsChecked == true, FuzzyGroups = fuzzy.Where(c => c.IsChecked == true).Select(c => (string)c.Tag).ToList(), Shortcuts = new(original.Shortcuts) };
        if (schema!.StartsWith("wanxiang_"))
        {
            value.Shortcuts.Clear(); foreach (string line in shortcuts.Text.Split('\n', StringSplitOptions.RemoveEmptyEntries)) { var parts = line.TrimEnd('\r').Split('\t', 2); if (parts.Length != 2 || !value.Shortcuts.TryAdd(parts[0], parts[1])) throw new InvalidDataException("快捷短语须用 Tab 分隔，缩写不能重复。"); }
        }
        return value;
    }
    public bool IsDirty
    {
        get { if (original == null) return false; try { return JsonSerializer.Serialize(original) != JsonSerializer.Serialize(Edited()); } catch { return true; } }
    }
    public ConfigurationPlan Preview()
    {
        if (original == null) return new([]);
        if ((File.Exists(customPath) ? File.ReadAllText(customPath) : "") != originalCustom) throw new IOException("配置已被其他程序修改，请重新载入。");
        if (shortcutsPath.Length > 0 && (File.Exists(shortcutsPath) ? File.ReadAllText(shortcutsPath) : "") != originalShortcuts) throw new IOException("快捷短语已被其他程序修改，请重新载入。");
        return new(new RimeProfiles(context.Root).PreviewScheme(schema!, Edited()));
    }
    public void Saved() { }
}
