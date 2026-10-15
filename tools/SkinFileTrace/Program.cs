using System.Diagnostics;
using System.Globalization;
using System.Security.Principal;
using System.Text.Json;
using Microsoft.Diagnostics.Tracing;
using Microsoft.Diagnostics.Tracing.Session;
using SkinFileTrace;

try
{
    if (args is ["--self-test"]) { FileRequests.SelfTest(); return 0; }
    if (args is ["--help"] or [])
    {
        Console.WriteLine("SkinFileTrace --pids 1234,5678 --assets a.png,b.png --seconds 30 --output trace.jsonl\n" +
            "Run from an administrator terminal. Real-time only; no ETL or global file log is saved.\n" +
            "--self-test tests filtering without tracing; --trace-self-test --output FILE checks real Windows ETW using private fixtures.");
        return 0;
    }
    if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException("Windows only");
    if (!new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator))
        throw new UnauthorizedAccessException("ETW requires an administrator terminal. No trace was started.");

    bool smoke = args is ["--trace-self-test", "--output", _];
    var options = smoke ? new Dictionary<string, string>() : Parse(args);
    var processes = smoke ? new HashSet<int> { Environment.ProcessId } :
        options["--pids"].Split(',').Select(int.Parse).ToHashSet();
    if (processes.Count is < 1 or > 8 || processes.Any(pid => pid <= 4))
        throw new ArgumentException("Specify 1 to 8 live, non-System PIDs");
    // Validate before starting; a process restart requires a new capture/PID.
    foreach (int pid in processes) using (Process.GetProcessById(pid)) { }
    var assets = new HashSet<string>((smoke ? "skin-trace-fixture.png" : options["--assets"]).Split(','),
        StringComparer.OrdinalIgnoreCase);
    if (assets.Count is < 1 or > 8 || assets.Any(name => !name.EndsWith(".png", StringComparison.OrdinalIgnoreCase) ||
        name.Length > 128 || name.IndexOfAny(['\\', '/', ':', '*', '?', '"', '<', '>', '|']) >= 0))
        throw new ArgumentException("Specify 1 to 8 PNG basenames, without wildcards or directory paths");
    int seconds = smoke ? 4 : int.Parse(options["--seconds"]);
    if (seconds is < 3 or > 60) throw new ArgumentException("Capture duration must be 3 to 60 seconds");
    string? fixtureRoot = smoke ? Path.Combine(Path.GetTempPath(), "RimeSkinTrace-" + Guid.NewGuid().ToString("N")) : null;
    string output = Path.GetFullPath(smoke ? args[2] : options["--output"]);
    if (smoke) Directory.CreateDirectory(fixtureRoot!);
    using var writer = new StreamWriter(new FileStream(output, FileMode.CreateNew, FileAccess.Write, FileShare.Read)) { AutoFlush = true };
    void Write(object row) => writer.WriteLine(JsonSerializer.Serialize(row));
    var requests = new FileRequests(processes, assets, Write);
    string sessionName = "RimeSkinFileTrace-" + Guid.NewGuid().ToString("N");
    using var session = new TraceEventSession(sessionName, TraceEventSessionOptions.Create | TraceEventSessionOptions.NoRestartOnCreate);
    session.StopOnDispose = true;
    session.BufferSizeMB = 8;
    var source = session.Source;
    int decodeErrors = 0;
    // Dynamic uses the installed provider's TDH manifest. Unsupported schemas
    // are counted explicitly rather than guessed by fixed byte offsets.
    source.Dynamic.All += data =>
    {
        if (data.ProviderGuid != new Guid("edd08927-9cc4-4e65-b970-c2560fb5c289")) return;
        try
        {
            ulong Value(params string[] names)
            {
                foreach (var name in names)
                {
                    if (data.PayloadIndex(name) < 0) continue;
                    var value = data.PayloadByName(name);
                    if (value is long signed) return unchecked((ulong)signed);
                    if (value is int signed32) return unchecked((uint)signed32);
                    if (value is string text && text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                        return ulong.Parse(text.AsSpan(2), NumberStyles.HexNumber);
                    return Convert.ToUInt64(value, CultureInfo.InvariantCulture);
                }
                throw new InvalidDataException("Required event field missing");
            }
            if ((int)data.ID == 12)
            {
                if (!processes.Contains(data.ProcessID)) return;
                // TraceEvent's built-in kernel parser moves the issuing thread
                // into ThreadID; the manifest parser may expose it as payload.
                int tid = data.PayloadIndex("IssuingThreadId") >= 0 ? checked((int)Value("IssuingThreadId")) : data.ThreadID;
                string path = data.PayloadByName(data.PayloadIndex("FileName") >= 0 ? "FileName" : "OpenPath") as string
                    ?? throw new InvalidDataException("FileName/OpenPath missing");
                requests.Open(data.TimeStamp.ToUniversalTime(), data.ProcessID, tid,
                    Value("Irp", "IrpPtr"), path,
                    (uint)Value("CreateOptions"), (uint)Value("CreateAttributes", "FileAttributes"), (uint)Value("ShareAccess"));
            }
            else if ((int)data.ID == 24)
                requests.End(data.TimeStamp.ToUniversalTime(), Value("Irp", "IrpPtr"), (uint)Value("NtStatus", "Status"));
        }
        catch (Exception ex) when (ex is ArgumentException or InvalidDataException or InvalidCastException or FormatException or OverflowException)
        {
            if (++decodeErrors == 1) Write(new { kind = "schema_error", eventId = (int)data.ID, fields = data.PayloadNames });
        }
    };
    // System is included only for asynchronous operation completions. Open events
    // and all persisted paths still require an explicitly selected process/asset.
    session.EnableProvider(new Guid("edd08927-9cc4-4e65-b970-c2560fb5c289"), TraceEventLevel.Informational,
        0x80 | 0x40, new TraceEventProviderOptions
        {
            ProcessIDFilter = processes.Append(4).ToList(), EventIDsToEnable = [12, 24]
        });
    Write(new { kind = "start", time = DateTime.UtcNow, session = sessionName, pids = processes, assets, seconds });
    Console.WriteLine($"READY {seconds}s; session={sessionName}; output={output}");
    using var timer = new Timer(_ => session.Stop(noThrow: true), null, TimeSpan.FromSeconds(seconds), Timeout.InfiniteTimeSpan);
    ConsoleCancelEventHandler cancel = (_, e) => { e.Cancel = true; session.Stop(noThrow: true); };
    Console.CancelKeyPress += cancel;
    Task? fixtures = null;
    if (smoke) fixtures = Task.Run(async () =>
    {
        await Task.Delay(500);
        var path = Path.Combine(fixtureRoot!, "skin-trace-fixture.png");
        File.WriteAllBytes(path, [1]);
        for (int i = 0; i < 3; i++)
        {
            File.ReadAllBytes(path);
            try { File.ReadAllBytes(Path.Combine(fixtureRoot!, "missing", "skin-trace-fixture.png")); }
            catch (DirectoryNotFoundException) { }
            await Task.Delay(100);
        }
    });
    try { source.Process(); if (fixtures != null) await fixtures; }
    finally { Console.CancelKeyPress -= cancel; }
    Write(requests.Summary(source.EventsLost, decodeErrors));
    if (smoke && (requests.Opened == 0 || requests.Completed == 0 || requests.Failed == 0 || decodeErrors != 0 || source.EventsLost != 0))
        throw new InvalidOperationException($"ETW positive/failure controls missing; inspect {output}");
    Console.WriteLine($"DONE opens={requests.Opened}, completions={requests.Completed}, failures={requests.Failed}, lost={source.EventsLost}, schemaErrors={decodeErrors}");
    return requests.Opened == 0 || requests.Completed != requests.Opened || requests.Evicted > 0 ||
        decodeErrors > 0 || source.EventsLost > 0 ? 2 : 0;
}
catch (Exception ex)
{
    Console.Error.WriteLine(ex.Message);
    return 1;
}

static Dictionary<string, string> Parse(string[] args)
{
    string[] keys = ["--pids", "--assets", "--seconds", "--output"];
    var result = new Dictionary<string, string>();
    if (args.Length != keys.Length * 2) throw new ArgumentException("Use --help for capture arguments");
    for (int i = 0; i < args.Length; i += 2)
        if (!keys.Contains(args[i]) || !result.TryAdd(args[i], args[i + 1])) throw new ArgumentException("Unknown or repeated argument");
    if (keys.Any(key => !result.ContainsKey(key))) throw new ArgumentException("Missing argument");
    return result;
}
