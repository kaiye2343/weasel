using System.Windows;
using System.Windows.Controls;
using Shorekeeper.Core;

namespace Shorekeeper.Manager;
public static class ChangeReview
{
    public static bool Confirm(ConfigurationPlan plan, string action = "确认并备份保存", int? maxContentLength = null)
    {
        if (plan.Changes.Count == 0) throw new InvalidOperationException("没有需要保存的修改。");
        var panel = new DockPanel { Margin = new(24) };
        var buttons = new WrapPanel { HorizontalAlignment = HorizontalAlignment.Right };
        var window = new Window { Title = "审阅配置修改", Width = 900, Height = 680, Owner = Application.Current.MainWindow, WindowStartupLocation = WindowStartupLocation.CenterOwner, Content = panel };
        buttons.Children.Add(Elements.Button("取消", () => window.DialogResult = false)); buttons.Children.Add(Elements.Button(action, () => window.DialogResult = true, true));
        DockPanel.SetDock(buttons, Dock.Bottom); panel.Children.Add(buttons);
        string Display(string value) => maxContentLength is int limit && value.Length > limit ? value[..limit] + $"\n【预览截取前 {limit} 字符，全文 {value.Length:N0} 字符；保存及备份使用完整内容】" : value;
        var text = Elements.Editor(string.Join("\n\n", plan.Changes.Select(c => $"文件：{c.Path}\n\n修改前\n{Display(c.Before)}\n\n修改后\n{(c.After == null ? "【将移除此文件；原文件会先备份】" : Display(c.After))}")), true); text.IsReadOnly = true; text.FontFamily = new("Consolas"); text.HorizontalScrollBarVisibility = ScrollBarVisibility.Auto;
        panel.Children.Add(text); return window.ShowDialog() == true;
    }
}
