using System.Windows;
using System.Windows.Controls;
using Shorekeeper.Core;

namespace Shorekeeper.Manager;
public static class ChangeReview
{
    public static bool Confirm(ConfigurationPlan plan)
    {
        if (plan.Changes.Count == 0) throw new InvalidOperationException("没有需要保存的修改。");
        var panel = new DockPanel { Margin = new(24) };
        var buttons = new WrapPanel { HorizontalAlignment = HorizontalAlignment.Right };
        var window = new Window { Title = "审阅配置修改", Width = 900, Height = 680, Owner = Application.Current.MainWindow, WindowStartupLocation = WindowStartupLocation.CenterOwner, Content = panel };
        buttons.Children.Add(Elements.Button("取消", () => window.DialogResult = false)); buttons.Children.Add(Elements.Button("确认并备份保存", () => window.DialogResult = true, true));
        DockPanel.SetDock(buttons, Dock.Bottom); panel.Children.Add(buttons);
        var text = Elements.Editor(string.Join("\n\n", plan.Changes.Select(c => $"文件：{c.Path}\n\n保存前\n{c.Before}\n\n保存后\n{c.After}")), true); text.IsReadOnly = true; text.FontFamily = new("Consolas"); text.HorizontalScrollBarVisibility = ScrollBarVisibility.Auto;
        panel.Children.Add(text); return window.ShowDialog() == true;
    }
}
