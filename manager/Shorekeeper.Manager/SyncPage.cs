using System.Security.Cryptography;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using Shorekeeper.Core;

namespace Shorekeeper.Manager;
public sealed class SyncPage : UserControl
{
    public SyncPage(ManagerContext context)
    {
        var body = new StackPanel(); body.Children.Add(Elements.Text("手动 WebDAV 同步", 22)); body.Children.Add(Elements.Text("下载各设备官方快照 → Rime 合并 → 上传本设备快照。其他设备配置先暂存，不覆盖当前配置。", 14, true));
        var url = Elements.Editor("https://dav.jianguoyun.com/dav/"); var user = Elements.Editor(); var password = new PasswordBox { Padding = new(10), Margin = new(0, 4, 0, 14) }; var remember = new CheckBox { Content = "使用 Windows 凭据管理器保存应用密码", Margin = new(0, 8, 0, 16) };
        body.Children.Add(Elements.Text("WebDAV 地址（HTTPS）")); body.Children.Add(url); body.Children.Add(Elements.Text("账号")); body.Children.Add(user); body.Children.Add(Elements.Text("应用密码")); body.Children.Add(password); body.Children.Add(remember);
        body.Children.Add(Elements.Button("连接并同步", async () => await context.Run(this, async () =>
        {
            var runner = context.InstalledRunner(); var endpoint = new Uri(url.Text); string account = user.Text;
            string target = "ShorekeeperWebDAV:" + Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(endpoint.AbsoluteUri + "\n" + account)));
            string secret = string.IsNullOrEmpty(password.Password) ? CredentialStore.Read(target) ?? "" : password.Password;
            if (secret.Length == 0) throw new InvalidOperationException("请输入应用密码。");
            using var dav = new HttpWebDav(endpoint, account, secret);
            if (remember.IsChecked == true) CredentialStore.Save(target, account, secret); password.Clear(); context.Report("正在备份、下载并合并快照…");
            var result = await new SnapshotSync(dav, runner).Run(context.Root); context.Report($"同步已完成：下载 {result.Downloaded}，上传 {result.Uploaded}。待审阅配置：{result.IncomingConfigDirectory}");
        }), true));
        body.Children.Add(Elements.Text("仅手动执行，不增加常驻同步进程。实际服务器和跨端合并尚需单独授权验证。", 12, true)); Content = Elements.Scroll(Elements.Card(body));
    }
}
