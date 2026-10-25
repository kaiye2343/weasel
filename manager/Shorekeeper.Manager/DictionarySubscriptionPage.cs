using System.IO;
using System.Net.Http;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;
using Shorekeeper.Core;

namespace Shorekeeper.Manager;

public sealed class DictionarySubscriptionPage : UserControl
{
    readonly ManagerContext context;
    readonly DictionarySubscriptions library;
    readonly string? schema;
    readonly StackPanel installed = new();
    readonly TextBox id = Elements.Editor(), name = Elements.Editor(), url = Elements.Editor();

    public DictionarySubscriptionPage(ManagerContext context)
    {
        this.context = context; library = new(context.Root); schema = context.Schema;
        var panel = new StackPanel();
        panel.Children.Add(Elements.Text("按需下载，离线输入", 21));
        panel.Children.Add(Elements.Text("订阅只在你点击更新时联网；不会发送输入内容。下载后还需启用到具体方案，并点击底部“部署应用”。个人补词请使用单独词库，避免更新覆盖。", 13, true));
        var add = new StackPanel(); add.Children.Add(Elements.Text("添加扩展词库", 18));
        var sources = new ComboBox { ItemsSource = DictionarySubscriptions.Catalog.Append(new("", "自定义来源／本地词库", "", "填写名称、标识和 HTTPS 直链；本地词库可不填更新地址。")), DisplayMemberPath = "Name" };
        var help = Elements.Text("可选择预设来源，或填写自定义 HTTPS .dict.yaml 直链。只接受自包含静态词库，不执行脚本或安装程序。", 12, true);
        sources.SelectionChanged += (_, _) =>
        {
            if (sources.SelectedItem is not DictionarySource source) return;
            id.Text = source.Id; name.Text = source.Id.Length == 0 ? "" : source.Name; url.Text = source.Url; help.Text = source.Description;
        };
        add.Children.Add(sources); add.Children.Add(help);
        add.Children.Add(Elements.Text("词库标识（首次确定后保持不变）", 12, true)); add.Children.Add(id);
        add.Children.Add(Elements.Text("显示名称", 12, true)); add.Children.Add(name);
        add.Children.Add(Elements.Text("更新地址（HTTPS；本地导入可留空）", 12, true)); add.Children.Add(url);
        var buttons = new WrapPanel();
        buttons.Children.Add(Elements.Button("下载并添加订阅", async () => await context.Run(this, async () =>
        {
            string key = id.Text.Trim(), title = name.Text.Trim(), address = url.Text.Trim();
            await Download(key, title, address);
        }), true));
        buttons.Children.Add(Elements.Button("导入本地词库", async () =>
        {
            var dialog = new OpenFileDialog { Filter = "Rime 静态词库|*.dict.yaml" };
            if (dialog.ShowDialog() != true) return;
            string key = id.Text.Trim(), title = name.Text.Trim(), address = url.Text.Trim();
            await context.Run(this, async () =>
            {
                if (new FileInfo(dialog.FileName).Length > DictionarySubscriptions.MaxBytes) throw new InvalidDataException("词库超过 16 MiB。");
                var plan = await Task.Run(() => library.PreviewImport(key, title, address, File.ReadAllBytes(dialog.FileName)));
                await Save(plan, "确认来源、版本和文件后，备份并导入");
            });
        }));
        add.Children.Add(buttons);
        add.Children.Add(Elements.Text("预设来源：github.com/suiginko/moetype。不同游戏的收录及多音字仍取决于维护者；本页不自动猜读音。自定义来源请自行确认可信度和使用许可。", 12, true));
        panel.Children.Add(Elements.Card(add));
        panel.Children.Add(Elements.Text("已下载的词库", 21)); panel.Children.Add(installed);
        panel.Children.Add(Elements.Text("作用范围：" + (schema ?? "全局默认（请选择具体方案后启用）") + "\n保存目录：" + library.Library + "\n词库挂载通过 translator/packs/+ 完成，不修改基础词库或活动 .userdb。工作／日常需分别启用；同一词库更新会影响所有已挂载方案。", 12, true));
        Content = Elements.Scroll(panel); sources.SelectedIndex = 0; Reload();
    }

    async Task Download(string key, string title, string address)
    {
        DictionarySubscriptions.ValidateUrl(address);
        context.Report("正在下载并校验词库，尚未修改方案…");
        using var client = new HttpClient(new HttpClientHandler { AllowAutoRedirect = false, UseCookies = false }) { Timeout = TimeSpan.FromSeconds(90) };
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(90));
        byte[] bytes = await DictionarySubscriptions.Download(client, address, timeout.Token);
        var plan = await Task.Run(() => library.PreviewImport(key, title, address, bytes));
        await Save(plan, "确认来源、版本和文件后，备份并保存更新");
    }
    async Task Save(ConfigurationPlan plan, string action)
    {
        if (plan.Changes.Count == 0) { context.Report("内容没有变化，无需保存或部署。"); return; }
        if (!ChangeReview.Confirm(plan, action, 3000)) { context.Report("已取消，原词库和配置保持不变。"); return; }
        string backup = await Task.Run(() => plan.Save(library.Root));
        if (plan.Changes.Any(c => !c.Path.StartsWith(library.Library + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))) context.MarkSaved();
        Reload(); context.Report("词库操作已保存；如更改已启用词库，请点击“部署应用”。备份：" + backup);
    }
    void Reload()
    {
        installed.Children.Clear();
        var subscriptions = library.List();
        if (subscriptions.Count == 0) installed.Children.Add(Elements.Text("尚未下载词库。添加后先检查与方案的编码兼容性，再决定启用。", 13, true));
        foreach (var subscription in subscriptions)
        {
            var body = new StackPanel(); var revision = subscription.Revisions.First(r => r.Hash == subscription.CurrentHash);
            body.Children.Add(Elements.Text(subscription.Name, 18));
            body.Children.Add(Elements.Text($"{revision.Entries:N0} 条 · 版本 {revision.Version} · 下载于 {revision.DownloadedAt.ToLocalTime():yyyy-MM-dd HH:mm}\n来源：{(subscription.Url.Length == 0 ? "本地导入" : subscription.Url)}\nSHA256：{revision.Hash}", 12, true));
            var scopes = library.EnabledSchemas(subscription.Id);
            body.Children.Add(Elements.Text("已保存的挂载方案：" + (scopes.Count == 0 ? "未启用" : string.Join("、", scopes)) + "（实际生效需部署）", 12, true));
            var actions = new WrapPanel();
            var update = Elements.Button("检查并下载更新", async () => await context.Run(this, () => Download(subscription.Id, subscription.Name, subscription.Url)));
            update.IsEnabled = subscription.Url.Length != 0; actions.Children.Add(update);
            bool enabled = library.IsEnabled(subscription.Id, schema);
            var toggle = Elements.Button(enabled ? "从当前方案停用" : "启用到当前方案", async () => await context.Run(this, async () =>
            {
                context.Report("正在检查词库编码和方案补丁…");
                var plan = await Task.Run(() => library.PreviewEnable(subscription.Id, schema!, !enabled));
                await Save(plan, "备份并保存挂载修改（稍后部署）");
            }));
            toggle.IsEnabled = schema != null; actions.Children.Add(toggle);
            actions.Children.Add(Elements.Button("导出词库", async () => await context.Run(this, () =>
            {
                var dialog = new SaveFileDialog { Filter = "Rime 静态词库|*.dict.yaml", FileName = subscription.DictionaryName + ".dict.yaml" };
                if (dialog.ShowDialog() == true) { library.Export(subscription.Id, dialog.FileName); context.Report("词库已导出。"); }
                return Task.CompletedTask;
            })));
            actions.Children.Add(Elements.Button("移除订阅", async () => await context.Run(this, async () =>
            {
                var plan = await Task.Run(() => library.PreviewRemove(subscription.Id));
                await Save(plan, "备份并移除订阅（历史下载与个人词库保留）");
            })));
            body.Children.Add(actions);
            var history = new ComboBox();
            // Include the digest because upstream version labels may remain unchanged between updates.
            history.ItemsSource = subscription.Revisions.Select(r => new RevisionChoice(r.Hash, $"{r.Version} · {r.DownloadedAt.ToLocalTime():yyyy-MM-dd HH:mm} · {r.Hash[..10]}")).ToList();
            history.DisplayMemberPath = "Label"; history.SelectedValuePath = "Hash"; history.SelectedValue = revision.Hash;
            body.Children.Add(Elements.Text("已下载版本（可回退）", 12, true)); body.Children.Add(history);
            body.Children.Add(Elements.Button("切换到所选版本", async () => await context.Run(this, async () =>
            {
                if (history.SelectedValue is not string hash) throw new InvalidOperationException("请选择版本。");
                var plan = await Task.Run(() => library.PreviewRollback(subscription.Id, hash));
                await Save(plan, "备份并切换版本（稍后部署）");
            })));
            installed.Children.Add(Elements.Card(body));
        }
    }
    sealed record RevisionChoice(string Hash, string Label);
}
