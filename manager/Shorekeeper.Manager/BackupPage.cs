using System.IO;
using System.Windows;
using System.Windows.Controls;
using Shorekeeper.Core;

namespace Shorekeeper.Manager;
public sealed class BackupPage : UserControl
{
    public BackupPage(ManagerContext context)
    {
        string root = Path.Combine(context.Root, "backups", "shorekeeper");
        var panel = new StackPanel(); panel.Children.Add(Elements.Text("配置保存历史", 22)); panel.Children.Add(Elements.Text("每次保存先备份原文件，并记录修改前后内容。恢复后仍需部署应用。", 13, true));
        var list = new ListBox { ItemsSource = Directory.Exists(root) ? Directory.GetDirectories(root).OrderDescending().ToList() : [], MaxHeight = 350 };
        panel.Children.Add(list);
        panel.Children.Add(Elements.Button("查看修改记录", () =>
        {
            if (list.SelectedItem is not string directory) return;
            string file = Path.Combine(directory, "changes.json");
            var window = new Window { Title = "配置修改记录", Width = 850, Height = 600, Owner = Application.Current.MainWindow, Content = Elements.Editor(File.Exists(file) ? File.ReadAllText(file) : "此旧版备份没有变更清单；可打开目录查看原文件。", true) };
            ((TextBox)window.Content).IsReadOnly = true; window.ShowDialog();
        }));
        panel.Children.Add(Elements.Button("打开备份目录", () => { try { ManagerContext.OpenDirectory(list.SelectedItem as string ?? root); } catch (Exception e) { context.Report(e.Message); } }));
        panel.Children.Add(Elements.Button("恢复选中的配置", async () => await context.Run(this, () =>
        {
            if (list.SelectedItem is not string directory) throw new InvalidOperationException("请选择备份。");
            var plan = RimeSettings.PreviewRestore(context.Root, directory);
            if (!ChangeReview.Confirm(plan, "确认文件变更并备份恢复")) return Task.CompletedTask;
            plan.Save(context.Root); context.MarkSaved(); context.Report("配置已恢复待部署。当前文件也已备份。"); return Task.CompletedTask;
        })));
        Content = Elements.Scroll(Elements.Card(panel));
    }
}
