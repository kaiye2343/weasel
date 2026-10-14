using System.Diagnostics;
using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media.Imaging;
using Microsoft.Win32;
using Shorekeeper.Core;

namespace Shorekeeper.Manager;
public partial class MainWindow : Window
{
    readonly SkinStore store;
    readonly CancellationTokenSource lifetime = new();
    string? loadedProfile;
    InputSettings inputSettings = new();
    sealed record SkinEntry(InstalledSkin Skin) { public string Display => $"{Skin.Manifest.Name}  {Skin.Manifest.Version}"; }
    SkinEntry? Current => SkinList.SelectedItem as SkinEntry;
    SkinVariant? Variant => VariantList.SelectedItem as SkinVariant;
    string ToolDirectory => AppContext.BaseDirectory;
    public MainWindow()
    {
        InitializeComponent();
        string[] args = Environment.GetCommandLineArgs(); int index = Array.IndexOf(args, "--data-root");
        store = new SkinStore(index >= 0 && index + 1 < args.Length ? args[index + 1] : null);
        RimeDirectory.Text = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Rime");
        InputMode.ItemsSource = new[] { "全拼", "自然码", "小鹤双拼", "微软双拼", "搜狗双拼", "智能ABC", "紫光双拼", "拼音加加" };
        foreach (var name in RimeProfiles.FuzzyNames) FuzzyOptions.Children.Add(new CheckBox { Content = name.Replace('_', '/'), Tag = name, Margin = new Thickness(0, 4, 20, 8) });
        Closing += (_, _) => lifetime.Cancel();
        Loaded += async (_, _) => await Work(async () => { await RefreshSkins(); StatusText.Text = "就绪 · 本版为实验构建，先用原生预览验证外观。"; });
    }
    async Task Work(Func<Task> action)
    {
        if (!IsEnabled) return;
        IsEnabled = false;
        try { await action(); }
        catch (OperationCanceledException) { StatusText.Text = "操作已取消。"; }
        catch (Exception error) { StatusText.Text = "未完成：" + error.Message; }
        finally { IsEnabled = true; }
    }
    async Task RefreshSkins()
    {
        var skins = await Task.Run(store.List); SkinList.ItemsSource = skins.Select(s => new SkinEntry(s)).ToList();
        var state = store.Selection();
        SkinList.SelectedItem = SkinList.Items.Cast<SkinEntry>().FirstOrDefault(s => s.Skin.Manifest.Id == state.SkinId && s.Skin.Manifest.Version == state.SkinVersion) ?? SkinList.Items.Cast<SkinEntry>().FirstOrDefault();
        if (state.VariantId != null) VariantList.SelectedItem = VariantList.Items.Cast<SkinVariant>().FirstOrDefault(v => v.Id == state.VariantId) ?? VariantList.SelectedItem;
        if (state.StyleOverride != null && state.SkinId == Current?.Skin.Manifest.Id && state.SkinVersion == Current?.Skin.Manifest.Version) ShowStyle(state.StyleOverride);
    }
    void SkinChanged(object sender, SelectionChangedEventArgs e)
    {
        if (Current is not { } current) return;
        SkinName.Text = current.Display; VariantList.ItemsSource = current.Skin.Manifest.Variants; VariantList.SelectedIndex = 0;
        var image = new BitmapImage(); image.BeginInit(); image.CacheOption = BitmapCacheOption.OnLoad;
        image.UriSource = new Uri(SkinStore.ResolveAsset(current.Skin.Directory, current.Skin.Manifest.Preview)); image.EndInit(); image.Freeze(); Thumbnail.Source = image;
    }
    void VariantChanged(object sender, SelectionChangedEventArgs e) { if (Variant != null) ShowStyle(Variant.Style); }
    void ShowStyle(SkinStyle style)
    {
        StyleEditor.Text = JsonSerializer.Serialize(style, SkinJson.Options);
        FontSizeInput.Text = style.FontPoint.ToString(); RightPadding.Text = style.PaddingRight.ToString();
        AnnotationPosition.SelectedIndex = style.AnnotationPosition == "above" ? 0 : style.AnnotationPosition == "beside" ? 1 : 2;
        BorderKind.SelectedIndex = style.BorderStyle == "none" ? 0 : style.BorderStyle == "solid" ? 1 : 2;
    }
    SkinStyle EditedStyle()
    {
        var style = JsonSerializer.Deserialize<SkinStyle>(StyleEditor.Text, SkinJson.Options) ?? throw new InvalidDataException("样式为空。");
        style.FontPoint = int.Parse(FontSizeInput.Text); style.PaddingRight = int.Parse(RightPadding.Text);
        style.AnnotationPosition = AnnotationPosition.SelectedIndex switch { 0 => "above", 1 => "beside", _ => "hidden" };
        style.BorderStyle = BorderKind.SelectedIndex switch { 0 => "none", 1 => "solid", _ => "dash" };
        SkinStore.ValidateStyle(style); return style;
    }
    async void ImportSkin(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog { Filter = "皮肤包|*.rimeskin;*.zip" }; if (dialog.ShowDialog() != true) return;
        await Work(async () => { var skin = await Task.Run(() => store.Import(dialog.FileName)); await RefreshSkins(); SkinList.SelectedItem = SkinList.Items.Cast<SkinEntry>().First(s => s.Skin.Directory == skin.Directory); StatusText.Text = "已导入，尚未启用。可以先打开原生预览。"; });
    }
    async void PreviewSkin(object sender, RoutedEventArgs e) => await Work(async () =>
    {
        if (Current == null || Variant == null) throw new InvalidOperationException("请先选择皮肤与变体。");
        string exe = Path.Combine(ToolDirectory, "WeaselSkinPreview.exe"); if (!File.Exists(exe)) throw new FileNotFoundException("请使用包含原生预览程序的完整工具包。");
        string directory = Path.Combine(store.Root, "previews"); Directory.CreateDirectory(directory);
        string file = Path.Combine(directory, Guid.NewGuid().ToString("N") + ".json"); SkinJson.WriteAtomic(file, EditedStyle());
        var start = new ProcessStartInfo(exe) { UseShellExecute = false, WorkingDirectory = ToolDirectory };
        foreach (string arg in new[] { "--manifest", Path.Combine(Current.Skin.Directory, "manifest.json"), "--variant", Variant.Id, "--style-overrides", file }) start.ArgumentList.Add(arg);
        var process = Process.Start(start) ?? throw new IOException("无法启动预览。");
        _ = process.WaitForExitAsync().ContinueWith(_ => { process.Dispose(); try { File.Delete(file); } catch (IOException) { } });
        StatusText.Text = "已打开隔离预览；不会连接正在使用的输入法。"; await Task.CompletedTask;
    });
    async Task RefreshEngine()
    {
        try { await new RimeProcess(ToolDirectory).RunAsync("skin-reload", lifetime.Token); StatusText.Text = "外观已即时刷新；没有部署或重启Rime。"; }
        catch (FileNotFoundException) { StatusText.Text = "选择已保存；此工具目录缺少即时刷新组件。"; }
        catch (RimeOperationException error) when (error.ExitCode == 2) { StatusText.Text = "选择已保存；小狼毫服务未运行，将在下次启动时加载。"; }
    }
    async void ApplySkin(object sender, RoutedEventArgs e) => await Work(async () =>
    {
        if (Current == null || Variant == null) throw new InvalidOperationException("请先选择皮肤。");
        var previous = store.Selection();
        var skin = Current.Skin; string variant = Variant.Id; var style = EditedStyle();
        await Task.Run(() => store.Select(skin, variant, style));
        try { await RefreshEngine(); }
        catch { SkinJson.WriteAtomic(store.StatePath, previous); throw; }
    });
    async void ResetSkin(object sender, RoutedEventArgs e) => await Work(async () =>
    {
        var previous = store.Selection(); store.Reset();
        try { await RefreshEngine(); } catch { SkinJson.WriteAtomic(store.StatePath, previous); throw; }
    });
    async void ExportSkin(object sender, RoutedEventArgs e)
    {
        if (Current == null) return; var current = Current;
        var dialog = new SaveFileDialog { Filter = "皮肤包|*.rimeskin", FileName = current.Skin.Manifest.Id + ".rimeskin" }; if (dialog.ShowDialog() != true) return;
        await Work(async () => { await Task.Run(() => store.Export(current.Skin, dialog.FileName)); StatusText.Text = "皮肤包已导出。"; });
    }
    void ShowInput(InputSettings settings)
    {
        inputSettings = settings; InputMode.SelectedItem = settings.PinyinMode;
        SimplifiedInput.IsChecked = settings.Simplified; CorrectionInput.IsChecked = settings.EnableCorrection; ContextInput.IsChecked = settings.ContextReorder;
        foreach (CheckBox check in FuzzyOptions.Children) check.IsChecked = settings.FuzzyGroups.Contains((string)check.Tag);
        InputEditor.Text = JsonSerializer.Serialize(settings, SkinJson.Options);
        ShortcutsInput.Text = string.Join('\n', settings.Shortcuts.Select(p => p.Key + "\t" + p.Value));
    }
    async void LoadInputSettings(object sender, RoutedEventArgs e) => await Work(async () =>
    {
        var profiles = new RimeProfiles(RimeDirectory.Text); string id = ProfileId.Text;
        var settings = await Task.Run(() => profiles.Load(id)); loadedProfile = id; ShowInput(settings); StatusText.Text = "已载入场景设置，尚未修改文件。";
    });
    InputSettings EditedInput()
    {
        if (loadedProfile != ProfileId.Text) throw new InvalidOperationException("请先载入当前场景设置，避免覆盖其他场景。");
        var settings = JsonSerializer.Deserialize<InputSettings>(InputEditor.Text, SkinJson.Options) ?? throw new InvalidDataException("输入设置为空。");
        settings.PinyinMode = InputMode.SelectedItem as string ?? "全拼"; settings.Simplified = SimplifiedInput.IsChecked == true;
        settings.EnableCorrection = CorrectionInput.IsChecked == true; settings.ContextReorder = ContextInput.IsChecked == true;
        settings.FuzzyGroups = FuzzyOptions.Children.OfType<CheckBox>().Where(c => c.IsChecked == true).Select(c => (string)c.Tag).ToList();
        settings.Shortcuts.Clear();
        foreach (string line in ShortcutsInput.Text.Split('\n', StringSplitOptions.RemoveEmptyEntries)) { var pair = line.TrimEnd('\r').Split('\t', 2); if (pair.Length != 2) throw new InvalidDataException("快捷短语须为缩写<Tab>短语。"); settings.Shortcuts.Add(pair[0], pair[1]); }
        return settings;
    }
    string InstalledDirectory()
    {
        using var key = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\WOW6432Node\Rime\Weasel");
        string? directory = key?.GetValue("WeaselRoot") as string;
        if (directory == null) throw new IOException("未找到已安装的小狼毫。"); return directory;
    }
    async void SaveInputSettings(object sender, RoutedEventArgs e) => await Work(async () =>
    {
        string root = RimeDirectory.Text, id = ProfileId.Text; var settings = EditedInput();
        if (MessageBox.Show("将备份并保存当前场景设置，然后重新部署。YAML的注释和排版可能变化，原文件可从备份恢复。", "应用输入设置", MessageBoxButton.OKCancel) != MessageBoxResult.OK) return;
        string backup = await Task.Run(() => new RimeProfiles(root).Apply(id, settings));
        await new RimeProcess(InstalledDirectory()).RunAsync("deploy", lifetime.Token); StatusText.Text = "设置已应用。备份：" + backup;
    });
    async void CreateProfile(object sender, RoutedEventArgs e) => await Work(async () =>
    {
        string root = RimeDirectory.Text, id = ProfileId.Text;
        if (MessageBox.Show("将新建场景配置和独立学习数据命名，并追加到方案菜单。", "新建场景", MessageBoxButton.OKCancel) != MessageBoxResult.OK) return;
        await Task.Run(() => new RimeProfiles(root).Create(id)); await new RimeProcess(InstalledDirectory()).RunAsync("deploy", lifetime.Token); StatusText.Text = "场景已创建，可在方案菜单切换。";
    });
    async void ImportDictionary(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog { Filter = "词库TSV/文本|*.tsv;*.txt" }; if (dialog.ShowDialog() != true) return;
        await Work(async () => { string root = RimeDirectory.Text, id = ProfileId.Text; await Task.Run(() => new RimeProfiles(root).ImportDictionary(id, dialog.FileName)); await new RimeProcess(InstalledDirectory()).RunAsync("deploy", lifetime.Token); StatusText.Text = "词库已备份、合并并部署。"; });
    }
    async void BackupProfile(object sender, RoutedEventArgs e)
    {
        var dialog = new SaveFileDialog { Filter = "场景备份|*.zip", FileName = ProfileId.Text + "-backup.zip" }; if (dialog.ShowDialog() != true) return;
        await Work(async () => { await RimeSnapshots.BackupProfileAsync(RimeDirectory.Text, ProfileId.Text, dialog.FileName, new RimeProcess(InstalledDirectory())); StatusText.Text = "已备份场景配置与官方同步快照。"; });
    }
    async void RestoreProfile(object sender, RoutedEventArgs e)
    {
        var dialog = new OpenFileDialog { Filter = "场景备份|*.zip" }; if (dialog.ShowDialog() != true) return;
        if (MessageBox.Show("将先备份现有资料，再恢复配置，并由Rime合并用户词典快照。", "恢复备份", MessageBoxButton.OKCancel) != MessageBoxResult.OK) return;
        await Work(async () => { await RimeSnapshots.RestoreProfileAsync(RimeDirectory.Text, dialog.FileName, new RimeProcess(InstalledDirectory())); StatusText.Text = "配置已恢复，用户词典已交由官方同步机制合并。"; });
    }
    async void ExportAndroid(object sender, RoutedEventArgs e)
    {
        var dialog = new SaveFileDialog { Filter = "Android配置包|*.zip", FileName = "wanxiang-trime.zip" }; if (dialog.ShowDialog() != true) return;
        await Work(async () => { string root = RimeDirectory.Text; await Task.Run(() => new RimeProfiles(root).ExportAndroid(dialog.FileName)); StatusText.Text = "已导出Trime静态配置包；语法模型需单独导入，不包含Windows设备ID。"; });
    }
    async void SyncNow(object sender, RoutedEventArgs e) => await Work(async () =>
    {
        var endpoint = new Uri(DavUrl.Text); string user = DavUser.Text;
        string target = "ShorekeeperWebDAV:" + Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(endpoint.AbsoluteUri + "\n" + user)));
        string password = string.IsNullOrEmpty(DavPassword.Password) ? CredentialStore.Read(target) ?? "" : DavPassword.Password;
        if (password.Length == 0) throw new InvalidDataException("请输入应用密码。");
        using var dav = new HttpWebDav(endpoint, user, password);
        if (RememberCredential.IsChecked == true) CredentialStore.Save(target, user, password);
        DavPassword.Clear(); StatusText.Text = "正在备份、下载并合并快照…";
        var result = await new SnapshotSync(dav, new RimeProcess(InstalledDirectory())).Run(RimeDirectory.Text, lifetime.Token);
        StatusText.Text = $"同步完成：下载{result.Downloaded}，上传{result.Uploaded}。其他设备配置存放于{result.IncomingConfigDirectory}，待审阅后应用。";
    });
    async void CollectDiagnostics(object sender, RoutedEventArgs e) => await Work(async () =>
    {
        string root = RimeDirectory.Text;
        try { await new RimeProcess(ToolDirectory).RunAsync("diagnostics", lifetime.Token); } catch (IOException) { }
        string directory = await Task.Run(() => RimeSnapshots.CollectDiagnostics(root, store.Root)); DiagnosticResult.Text = directory;
        StatusText.Text = "诊断现场已保存；未重启小狼毫。";
    });
}
