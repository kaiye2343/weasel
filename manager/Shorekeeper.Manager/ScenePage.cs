using System.IO;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;
using Shorekeeper.Core;

namespace Shorekeeper.Manager;
public sealed class ScenePage : UserControl
{
    public ScenePage(ManagerContext context)
    {
        var body = new StackPanel(); body.Children.Add(Elements.Text("独立学习场景", 22));
        body.Children.Add(Elements.Text("使用万象基础词库，分别保存动态词频、自动造词和场景短语。创建后保存配置，再通过“部署应用”加入方案菜单。", 14, true));
        var ids = new RimeSettings(context.Root).Schemas().Where(s => s.Id.StartsWith("wanxiang_")).Select(s => s.Id[9..]).ToList();
        var current = new ComboBox { ItemsSource = ids, SelectedIndex = ids.Count > 0 ? 0 : -1 }; body.Children.Add(Elements.Text("已有场景")); body.Children.Add(current);
        var id = Elements.Editor("work"); body.Children.Add(Elements.Text("新场景标识（英文、数字、下划线）")); body.Children.Add(id);
        body.Children.Add(Elements.Button("创建独立场景", async () => await context.Run(this, async () =>
        {
            var profiles = new RimeProfiles(context.Root);
            if (MessageBox.Show("将备份配置并追加一个场景。不会部署或重启输入法。", "创建场景", MessageBoxButton.OKCancel) != MessageBoxResult.OK) return;
            string value = id.Text; await Task.Run(() => profiles.Create(value)); ids.Add(value); current.ItemsSource = ids.ToList(); current.SelectedItem = value; context.MarkSaved(); context.Report("场景已保存待部署。部署后用方案切换快捷键切换，无需再次部署。");
        })));
        string Selected() => current.SelectedItem as string ?? throw new InvalidOperationException("请选择已有场景。");
        body.Children.Add(Elements.Button("导入场景词库", async () =>
        {
            string selected; try { selected = Selected(); } catch (Exception e) { context.Report(e.Message); return; }
            var dialog = new OpenFileDialog { Filter = "词库 TSV / 文本|*.tsv;*.txt" }; if (dialog.ShowDialog() != true) return;
            await context.Run(this, () => { var plan = new ConfigurationPlan([new RimeProfiles(context.Root).PreviewDictionary(Path.Combine(context.Root, selected + ".dict.yaml"), dialog.FileName)]); if (ChangeReview.Confirm(plan)) { string backup = plan.Save(context.Root); context.MarkSaved(); context.Report("场景词库已保存待部署。备份：" + backup); } return Task.CompletedTask; });
        }));
        body.Children.Add(Elements.Button("备份场景及官方快照", async () =>
        {
            string selected; try { selected = Selected(); } catch (Exception e) { context.Report(e.Message); return; }
            var dialog = new SaveFileDialog { Filter = "场景备份|*.zip", FileName = selected + "-backup.zip" }; if (dialog.ShowDialog() != true) return;
            await context.Run(this, async () => { await RimeSnapshots.BackupProfileAsync(context.Root, selected, dialog.FileName, context.InstalledRunner()); context.Report("场景及官方快照已备份。"); });
        }));
        body.Children.Add(Elements.Button("恢复场景与合并快照", async () =>
        {
            var dialog = new OpenFileDialog { Filter = "场景备份|*.zip" }; if (dialog.ShowDialog() != true) return;
            if (MessageBox.Show("将先备份，恢复场景配置并调用官方部署与同步合并；不会覆盖活动 .userdb。", "恢复场景", MessageBoxButton.OKCancel) != MessageBoxResult.OK) return;
            await context.Run(this, async () => { await RimeSnapshots.RestoreProfileAsync(context.Root, dialog.FileName, context.InstalledRunner()); context.Report("已应用场景配置，并交由 Rime 合并快照。"); });
        }));
        body.Children.Add(Elements.Text("编辑场景的全／双拼、模糊音和快捷短语：在顶部选择 wanxiang_场景，再打开“万象输入”。", 13, true));
        Content = Elements.Scroll(Elements.Card(body));
    }
}
