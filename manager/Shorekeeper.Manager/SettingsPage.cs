using System.Windows;
using System.Windows.Controls;
using Shorekeeper.Core;

namespace Shorekeeper.Manager;

public sealed class SettingsPage : UserControl, ISettingsPage
{
    sealed record Choice(string Value, string Label);
    static string ChoiceLabel(string value) => value switch { "noop" => "不操作", "inline_ascii" => "内嵌英文", "commit_text" => "上屏文字并切换", "commit_code" => "上屏编码并切换", "clear" => "清空并切换", "" => "继承方案默认", _ => value };
    readonly ManagerContext context;
    readonly RimeSettings settings;
    readonly string? scope;
    readonly List<(SettingValue Value, Func<string> Get)> editors = [];
    DisplaySettingsPanel? displayPanel;
    public SettingsPage(ManagerContext context, string module)
    {
        this.context = context; settings = new(context.Root); scope = context.Schema;
        var panel = new StackPanel();
        foreach (var definition in RimeSettings.Definitions.Where(d => d.Module == module))
        {
            var value = settings.Read(definition, scope);
            var body = new StackPanel(); body.Children.Add(Elements.Text(definition.Label, 18));
            body.Children.Add(Elements.Text("作用范围：" + (definition.WeaselOnly ? "全局 · 小狼毫" : scope ?? "全局默认") + "    来源：" + value.Source, 12, true));
            if (!value.Supported) body.Children.Add(Elements.Text(value.Source, 14, true));
            else
            {
                Func<string> get;
                if (definition.Kind == "bool")
                {
                    var check = new CheckBox { Content = "启用", IsChecked = value.Value == "true", Margin = new(0, 8, 0, 12) };
                    check.Checked += (_, _) => context.MarkChanged(); check.Unchecked += (_, _) => context.MarkChanged(); body.Children.Add(check); get = () => check.IsChecked == true ? "true" : "false";
                }
                else if (definition.Kind is "choice" or "model")
                {
                    var options = (definition.Choices ?? settings.Models().Prepend("").ToArray()).Append(value.Value).Distinct().Select(s => new Choice(s, ChoiceLabel(s))).ToList();
                    var combo = new ComboBox { ItemsSource = options, DisplayMemberPath = "Label", SelectedValuePath = "Value", SelectedValue = value.Value };
                    combo.SelectionChanged += (_, _) => context.MarkChanged(); body.Children.Add(combo); get = () => combo.SelectedValue as string ?? value.Value;
                }
                else
                {
                    var editor = Elements.Editor(value.Value, definition.Kind.StartsWith("yaml-")); editor.TextChanged += (_, _) => context.MarkChanged(); body.Children.Add(editor); get = () => editor.Text;
                }
                editors.Add((value, get)); body.Children.Add(Elements.Text(definition.Help, 12, true));
            }
            panel.Children.Add(Elements.Card(body));
        }
        if (module == "general")
        {
            displayPanel = new DisplaySettingsPanel(context);
            panel.Children.Add(Elements.Card(displayPanel));
        }
        if (module == "dictionary")
        {
            var body = new StackPanel(); body.Children.Add(Elements.Text("模型文件", 18));
            body.Children.Add(Elements.Text(string.Join("\n", settings.Models().DefaultIfEmpty("用户目录中未发现 .gram 模型。")), 14, true));
            body.Children.Add(Elements.Button("导入 .gram 模型", async () =>
            {
                var dialog = new Microsoft.Win32.OpenFileDialog { Filter = "语法模型|*.gram" }; if (dialog.ShowDialog() != true) return;
                await context.Run(this, async () => { var file = await Task.Run(() => settings.ImportModel(dialog.FileName)); context.Report("模型已导入，未改变当前方案：" + file + "；重新载入后选择模型。"); });
            }));
            body.Children.Add(Elements.Button("生成官方用户词典快照", async () => await context.Run(this, async () =>
            { var runner = context.InstalledRunner(); RimeSnapshots.RequireDefaultSyncDirectory(context.Root); RimeSnapshots.BackupAll(context.Root); await runner.RunAsync("sync"); context.Report("Rime 官方同步已执行，快照目录：" + System.IO.Path.Combine(context.Root, "sync")); })));
            body.Children.Add(Elements.Button("导入 TSV 到静态词库", async () =>
            {
                var source = new Microsoft.Win32.OpenFileDialog { Filter = "词库 TSV / 文本|*.tsv;*.txt" }; if (source.ShowDialog() != true) return;
                var target = new Microsoft.Win32.OpenFileDialog { Filter = "已有静态词库|*.dict.yaml", InitialDirectory = context.Root, Title = "选择要合并的静态词库（修改共享词库会影响引用它的方案）" }; if (target.ShowDialog() != true) return;
                await context.Run(this, () =>
                {
                    var change = new RimeProfiles(context.Root).PreviewDictionary(target.FileName, source.FileName);
                    if (!ChangeReview.Confirm(new([change]))) return Task.CompletedTask;
                    string backup = new ConfigurationPlan([change]).Save(context.Root); context.MarkSaved(); context.Report("词库已合并，待部署。原文件备份：" + backup); return Task.CompletedTask;
                });
            }));
            body.Children.Add(Elements.Text("场景自定义词库的 TSV 导入及快照备份、恢复位于“场景”。活动用户数据库不会被直接覆盖。", 12, true));
            panel.Children.Add(Elements.Card(body));
        }
        Content = Elements.Scroll(panel);
    }
    public bool IsDirty => editors.Any(e => e.Get() != e.Value.Value) || displayPanel?.IsDirty == true;
    public ConfigurationPlan? Preview()
    {
        if (displayPanel?.IsDirty == true) throw new InvalidOperationException("请先使用“保存并刷新候选显示”，显示偏好不参与 Rime 部署。");
        return settings.Preview(scope, editors.Where(e => e.Get() != e.Value.Value).Select(e => (e.Value.Definition, e.Get())));
    }
    public void Saved() { }
}
