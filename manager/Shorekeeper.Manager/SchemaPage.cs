using System.Collections.ObjectModel;
using System.Windows;
using System.Windows.Controls;
using Shorekeeper.Core;

namespace Shorekeeper.Manager;

public sealed class SchemaRow(SchemaInfo info)
{
    public SchemaInfo Info { get; } = info;
    public bool Enabled { get; set; } = info.Enabled;
    public string Name => Info.Name;
    public string Id => Info.Id;
    public string Details => $"版本：{Info.Version}    作者：{Info.Author}\n依赖：{Info.Dependencies}\n来源：{Info.Source}";
}
public sealed class SchemaPage : UserControl, ISettingsPage
{
    readonly RimeSettings settings;
    readonly ObservableCollection<SchemaRow> rows;
    readonly string[] initial;
    public SchemaPage(ManagerContext context)
    {
        settings = new(context.Root); rows = new(settings.Schemas().Select(s => new SchemaRow(s))); initial = rows.Where(r => r.Enabled).Select(r => r.Id).ToArray();
        var body = new StackPanel(); body.Children.Add(Elements.Text("启用的方案", 20)); body.Children.Add(Elements.Text("勾选以启用；使用上移／下移调整切换菜单顺序。缺失依赖不会被自动删除。", 13, true));
        var list = new ListBox { ItemsSource = rows, MinHeight = 260, MaxHeight = 430 };
        // Use a template so enabled state and dependency details remain visible.
        var factory = new FrameworkElementFactory(typeof(StackPanel)); factory.SetValue(StackPanel.MarginProperty, new Thickness(10));
        var check = new FrameworkElementFactory(typeof(CheckBox)); check.SetBinding(CheckBox.ContentProperty, new System.Windows.Data.Binding("Name")); check.SetBinding(CheckBox.IsCheckedProperty, new System.Windows.Data.Binding("Enabled") { Mode = System.Windows.Data.BindingMode.TwoWay });
        check.AddHandler(CheckBox.CheckedEvent, new RoutedEventHandler((_, _) => context.MarkChanged())); check.AddHandler(CheckBox.UncheckedEvent, new RoutedEventHandler((_, _) => context.MarkChanged())); factory.AppendChild(check);
        var details = new FrameworkElementFactory(typeof(TextBlock)); details.SetBinding(TextBlock.TextProperty, new System.Windows.Data.Binding("Details")); details.SetValue(TextBlock.FontSizeProperty, 12.0); details.SetValue(TextBlock.TextWrappingProperty, TextWrapping.Wrap); details.SetValue(TextBlock.MarginProperty, new Thickness(24, 6, 0, 0)); factory.AppendChild(details);
        list.ItemTemplate = new DataTemplate { VisualTree = factory }; body.Children.Add(list);
        var buttons = new WrapPanel();
        void Move(int direction) { int index = list.SelectedIndex, next = index + direction; if (index < 0 || next < 0 || next >= rows.Count) return; rows.Move(index, next); list.SelectedIndex = next; context.MarkChanged(); }
        buttons.Children.Add(Elements.Button("上移", () => Move(-1))); buttons.Children.Add(Elements.Button("下移", () => Move(1))); body.Children.Add(buttons);
        Content = Elements.Scroll(Elements.Card(body));
    }
    public bool IsDirty => !initial.SequenceEqual(rows.Where(r => r.Enabled).Select(r => r.Id));
    public ConfigurationPlan Preview() => IsDirty ? settings.PreviewSchemas(rows.Where(r => r.Enabled).Select(r => r.Id)) : new([]);
    public void Saved() { }
}
