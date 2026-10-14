using System.IO;
using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Principal;
using System.Text;
using System.Windows;
namespace Shorekeeper.Manager;
public partial class App : Application
{
    Mutex? instance;
    readonly CancellationTokenSource stopping = new();
    string pipeName = "";
    string? testRoot;
    [DllImport("user32.dll")] static extern bool AllowSetForegroundWindow(int processId);
    protected override void OnStartup(StartupEventArgs e)
    {
        bool test = e.Args.Contains("--instance-test");
        if (e.Args.Contains("--preview-mode") && !test || e.Args.Contains("--ui-smoke")) { base.OnStartup(e); OpenWindow(); return; }
        string identity = WindowsIdentity.GetCurrent().User!.Value;
        if (test)
        {
            int index = Array.IndexOf(e.Args, "--data-root");
            if (index < 0 || index + 1 == e.Args.Length) { Shutdown(1); return; }
            testRoot = Path.GetFullPath(e.Args[index + 1]);
            identity += Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(testRoot)))[..16];
        }
        pipeName = "RimeSettingsCenter." + identity;
        instance = new Mutex(true, @"Local\" + pipeName, out bool first);
        if (!first)
        {
            try
            {
                using var client = new NamedPipeClientStream(".", pipeName, PipeDirection.InOut, PipeOptions.CurrentUserOnly);
                client.Connect(3000);
                using var reader = new StreamReader(client, leaveOpen: true);
                using var writer = new StreamWriter(client, leaveOpen: true) { AutoFlush = true };
                if (int.TryParse(reader.ReadLine(), out int pid)) AllowSetForegroundWindow(pid);
                writer.WriteLine("activate");
            }
            catch (IOException) { if (!test) MessageBox.Show("设置中心正在启动，请稍后再打开。", "Rime 设置中心"); }
            catch (TimeoutException) { if (!test) MessageBox.Show("设置中心暂未响应，请检查已打开的窗口。", "Rime 设置中心"); }
            instance.Dispose(); instance = null; Shutdown(); return;
        }
        base.OnStartup(e);
        _ = Listen();
        if (test) _ = StopTestAfterTimeout();
        OpenWindow();
    }
    void OpenWindow() { MainWindow = new MainWindow(); MainWindow.Show(); }
    async Task StopTestAfterTimeout() { await Task.Delay(20000); if (!stopping.IsCancellationRequested) Shutdown(1); }
    async Task Listen()
    {
        while (!stopping.IsCancellationRequested)
        {
            try
            {
                using var server = new NamedPipeServerStream(pipeName, PipeDirection.InOut, 1, PipeTransmissionMode.Byte,
                    PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
                if (testRoot != null) { Directory.CreateDirectory(testRoot); File.WriteAllText(Path.Combine(testRoot, "instance-ready.txt"), Environment.ProcessId.ToString()); }
                await server.WaitForConnectionAsync(stopping.Token);
                using var writer = new StreamWriter(server, leaveOpen: true) { AutoFlush = true };
                using var reader = new StreamReader(server, leaveOpen: true);
                await writer.WriteLineAsync(Environment.ProcessId.ToString());
                if (await reader.ReadLineAsync(stopping.Token) != "activate") continue;
                await Dispatcher.InvokeAsync(() =>
                {
                    if (MainWindow is Window window) { window.Show(); if (window.WindowState == WindowState.Minimized) window.WindowState = WindowState.Normal; window.Activate(); }
                    if (testRoot != null) { File.WriteAllText(Path.Combine(testRoot, "instance-activated.txt"), Environment.ProcessId.ToString()); Shutdown(); }
                });
            }
            catch (OperationCanceledException) { break; }
            catch (IOException) { }
        }
    }
    protected override void OnExit(ExitEventArgs e)
    {
        stopping.Cancel();
        if (instance != null) { instance.ReleaseMutex(); instance.Dispose(); }
        base.OnExit(e);
    }
}
