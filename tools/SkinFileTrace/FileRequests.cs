using System.Text.Json;

namespace SkinFileTrace;

// Keep only explicitly requested PNG names and PIDs. Never retain other paths,
// payloads, stacks, file contents or input text from the real-time event stream.
internal sealed class FileRequests(HashSet<int> processes, HashSet<string> assets,
    Action<object> write)
{
    private readonly Dictionary<ulong, Request> pending = new();
    internal int Opened { get; private set; }
    internal int Completed { get; private set; }
    internal int Failed { get; private set; }
    internal int Evicted { get; private set; }
    internal int Pending => pending.Count;
    private sealed record Request(DateTime Time, int Pid, int Tid, string Path);

    internal void Open(DateTime time, int pid, int tid, ulong irp, string path,
        uint options, uint attributes, uint share)
    {
        // An IRP can be reused for an unrelated open. Remove stale correlations
        // before applying filters, so its completion cannot inherit an old path.
        pending.Remove(irp);
        if (!processes.Contains(pid) || !assets.Contains(FileName(path))) return;
        if (pending.Count >= 2048)
        {
            pending.Remove(pending.First().Key);
            Evicted++;
        }
        var request = new Request(time, pid, tid, path);
        if (irp != 0) pending[irp] = request;
        Opened++;
        write(new { kind = "open", time, pid, tid, irp = Hex(irp), path,
            options = Hex(options), attributes = Hex(attributes), share = Hex(share) });
    }

    internal void End(DateTime time, ulong irp, uint status)
    {
        if (!pending.Remove(irp, out var request)) return;
        Completed++;
        // NT_SUCCESS includes informational statuses, unlike Win32 error codes.
        if (unchecked((int)status) < 0) Failed++;
        write(new { kind = "complete", time, pid = request.Pid, tid = request.Tid,
            irp = Hex(irp), path = request.Path, ntStatus = $"0x{status:X8}",
            elapsedMs = (time - request.Time).TotalMilliseconds });
    }

    internal object Summary(int eventsLost, int decodeErrors) => new
    {
        kind = "summary", opened = Opened, completed = Completed, failed = Failed,
        unmatched = Pending, evicted = Evicted, eventsLost, decodeErrors,
        // A lack of events or completions is inconclusive, never a successful IO.
        usable = Opened > 0 && Completed == Opened && Evicted == 0 &&
                 eventsLost == 0 && decodeErrors == 0
    };

    internal static string FileName(string path) => path[(path.LastIndexOf('\\') + 1)..];
    internal static string Hex(ulong value) => $"0x{value:X}";

    internal static void SelfTest()
    {
        var rows = new List<string>();
        var requests = new FileRequests([42], new(StringComparer.OrdinalIgnoreCase) { "skin.png" },
            row => rows.Add(JsonSerializer.Serialize(row)));
        var now = DateTime.UtcNow;
        requests.Open(now, 43, 1, 1, @"C:\private\skin.png", 0, 0, 0);
        requests.Open(now, 42, 1, 2, @"C:\private\password.txt", 0, 0, 0);
        requests.End(now, 1, 0);
        requests.End(now, 2, 0);
        Check(rows.Count == 0, "reject unrelated PIDs and files");
        requests.Open(now, 42, 9, 3, @"\Device\Volume\skins\SKIN.PNG", 0x1200000, 0x80, 7);
        requests.End(now.AddMilliseconds(1), 3, 0xC000003A);
        requests.End(now, 3, 0);
        Check(requests.Opened == 1 && requests.Completed == 1 && requests.Failed == 1,
            "pair NT path-not-found once, including System-thread completions");
        Check(rows[1].Contains("0xC000003A") && rows[1].Contains("\"tid\":9"), "preserve failing status and issuing thread");
        requests.Open(now, 42, 9, 4, @"C:\skins\skin.png", 0, 0, 0);
        requests.Open(now, 43, 1, 4, @"C:\private\other.txt", 0, 0, 0);
        requests.End(now, 4, 0);
        Check(requests.Completed == 1 && requests.Pending == 0, "IRP reuse cannot misattribute completion");
        requests.Open(now, 42, 9, 0, @"C:\skins\skin.png", 0, 0, 0);
        requests.End(now, 0, 0);
        Check(requests.Pending == 0 && requests.Completed == 1, "unknown IRP cannot be correlated");
        for (ulong id = 10; id < 2060; id++)
            requests.Open(now, 42, 9, id, @"C:\skins\skin.png", 0, 0, 0);
        Check(requests.Pending == 2048 && requests.Evicted == 2, "bound unmatched requests");
        Check(!JsonSerializer.Serialize(requests.Summary(0, 0)).Contains("\"usable\":true"), "incomplete trace is inconclusive");
        Check(!string.Join('\n', rows).Contains("private"), "never persist unrelated paths");
        Console.WriteLine("PASS PID/asset filtering, NTSTATUS pairing, IRP reuse, missing completion and bounded state");
    }

    private static void Check(bool result, string message)
    {
        if (!result) throw new InvalidOperationException(message);
    }
}
