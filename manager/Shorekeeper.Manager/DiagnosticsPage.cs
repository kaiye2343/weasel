using System.IO;
using System.Windows.Controls;
using Shorekeeper.Core;

namespace Shorekeeper.Manager;
public sealed class DiagnosticsPage : UserControl
{
    public DiagnosticsPage(ManagerContext context)
    {
        var body = new StackPanel(); body.Children.Add(Elements.Text("问题1：先保存现场，再恢复", 22));
        body.Children.Add(Elements.Text("采集实际方案、配置来源、进程、版本、运行日志和配置哈希。此操作不会重启服务，也不记录当前输入内容。", 14, true));
        body.Children.Add(Elements.Button("保存诊断资料", async () => await context.Run(this, async () =>
        {
            bool runtime = false;
            if (!context.PreviewMode) try { await new RimeProcess(context.Tools).RunAsync("diagnostics"); runtime = true; } catch (IOException) { }
            string directory = await Task.Run(() => RimeSnapshots.CollectDiagnostics(context.Root, context.Skins.Root, runtime)); context.Report("现场已保存；未重启服务：" + directory);
        }), true));
        body.Children.Add(Elements.Text("尚未确认“问题1”的根因；不把安装方式或可视化面板当成已证实的原因。", 13, true)); Content = Elements.Card(body);
    }
}
