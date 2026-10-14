using System.Windows;
using System.Windows.Controls;
using Shorekeeper.Core;

namespace Shorekeeper.Manager;
public static class ChangeReview
{
    public static bool Confirm(ConfigurationPlan plan, string action = "确认并备份保存")
    {
        if (plan.Changes.Count == 0) throw new InvalidOperationException("没有需要保存的修改。");
        var panel = new DockPanel { Margin = new(24) };
        var buttons = new WrapPanel { HorizontalAlignment = HorizontalAlignment.Right };
        var window = new Window { Title = "审阅配置修改", Width = 900, Height = 680, Owner = Application.Current.MainWindow, WindowStartupLocation = WindowStartupLocation.CenterOwner, Content = panel };
        buttons.Children.Add(Elements.Button("取消", () => window.DialogResult = false)); buttons.Children.Add(Elements.Button(action, () => window.DialogResult = true, true));
        DockPanel.SetDock(buttons, Dock.Bottom); panel.Children.Add(buttons);
        var text = Elements.Editor(string.Join("\n\n", plan.Changes.Select(c => $"文件：{c.Path}\n\n修改前\n{c.Before}\n\n修改后\n{c.After ?? "【将移除此文件：该文件在所选备份之前不存在】"}")), true); text.IsReadOnly = true; text.FontFamily = new("Consolas"); text.HorizontalScrollBarVisibility = ScrollBarVisibility.Auto;
        panel.Children.Add(text); return window.ShowDialog() == true;
    }
}
