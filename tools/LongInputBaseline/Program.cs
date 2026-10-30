using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

if (args.SequenceEqual(new[] { "--self-test" })) { Baseline.SelfTest(); return; }
var options = new Dictionary<string, string>();
for (int i = 0; i < args.Length; i += 2)
{
    if (i + 1 >= args.Length || !new[] { "--engine", "--output", "--revision", "--warmup", "--repeats" }.Contains(args[i]) || !options.TryAdd(args[i], args[i + 1])) throw new ArgumentException("Provide unique --engine, --output, --revision and optional --warmup/--repeats");
}
await Baseline.Run(options);

internal sealed record InputCase(string Id, string Seed);
internal sealed record Corpus(int SchemaVersion, int[] Lengths, InputCase[] Cases);
internal sealed record Word(string Text, string Comment);
internal sealed record Sample(int KeyIndex, bool Accepted, double CoreMs, double ContextMs, double ExtractionMs, int Page, int Highlight, Word[] Candidates, string Digest);
internal sealed record Trial(string CaseId, int TargetLength, int Repetition, bool Warmup, Sample[] Samples);

internal static class Baseline
{
    const string CoreHash = "2D8F1BC3737635A11D9FB1BFCA4DC9E70533633930A8A0142A81CA879C39C45B";
    internal static readonly JsonSerializerOptions Json = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase, WriteIndented = true, PropertyNameCaseInsensitive = true };
    internal static double Percentile(IEnumerable<double> samples, double percentile)
    {
        var values = samples.Order().ToArray();
        if (values.Length == 0 || percentile is <= 0 or > 1) throw new ArgumentException("Nonempty samples and percentile (0,1] required");
        return values[(int)Math.Ceiling(values.Length * percentile) - 1];
    }
    static object Summary(IEnumerable<double> samples)
    {
        var a = samples.ToArray(); return new { count = a.Length, p50Ms = Percentile(a, .5), p95Ms = Percentile(a, .95), maxMs = a.Max(), minMs = a.Min() };
    }
    static string Hash(string file) { using var stream = File.OpenRead(file); return Convert.ToHexString(SHA256.HashData(stream)); }
    static Dictionary<string, string> Inventory(string root) => Directory.GetFiles(root, "*", SearchOption.AllDirectories).Order().ToDictionary(f => Path.GetRelativePath(root, f).Replace('\\', '/'), Hash);
    static double Ms(long ticks) => ticks * 1000.0 / Stopwatch.Frequency;
    internal static bool SameSemantics(Sample a, Sample b) => a.KeyIndex == b.KeyIndex && a.Accepted == b.Accepted && a.Page == b.Page && a.Highlight == b.Highlight && a.Candidates.SequenceEqual(b.Candidates);
    internal static async Task Run(Dictionary<string, string> options)
    {
        if (!OperatingSystem.IsWindows() || RuntimeInformation.ProcessArchitecture != Architecture.X64) throw new InvalidOperationException("Requires Windows x64");
        string Required(string key) => options.TryGetValue(key, out string? value) ? value : throw new ArgumentException("Missing " + key);
        string engine = Path.GetFullPath(Required("--engine")), output = Path.GetFullPath(Required("--output"));
        string revision = Required("--revision");
        if (revision.Length != 40 || !revision.All(Uri.IsHexDigit)) throw new ArgumentException("Revision must be a full source commit");
        int warmup = int.Parse(options.GetValueOrDefault("--warmup", "1")), repeats = int.Parse(options.GetValueOrDefault("--repeats", "5"));
        if (warmup is < 1 or > 5 || repeats is < 3 or > 10) throw new ArgumentException("Warmup 1..5, repeats 3..10");
        string allowed = Path.GetFullPath(".tools/long-input-baseline") + Path.DirectorySeparatorChar;
        if (!output.StartsWith(allowed, StringComparison.OrdinalIgnoreCase) || Directory.Exists(output) || File.Exists(output)) throw new ArgumentException("Output must be a fresh child of .tools/long-input-baseline");
        string fixture = Path.GetFullPath("tests/long-input-fixture");
        var fixtureFiles = Inventory(fixture);
        if (!fixtureFiles.Keys.Order().SequenceEqual(new[] { "baseline.dict.yaml", "baseline.schema.yaml", "corpus.json", "default.yaml" })) throw new InvalidDataException("Only the committed, self-contained fixture may be used");
        if (Hash(engine) != CoreHash) throw new InvalidDataException("Engine differs from pinned x64 librime 1.13.1; no performance comparison is valid");
        var corpus = JsonSerializer.Deserialize<Corpus>(File.ReadAllText(Path.Combine(fixture, "corpus.json")), Json) ?? throw new InvalidDataException("Missing corpus");
        if (corpus.SchemaVersion != 1 || corpus.Cases.Length is < 1 or > 8 || corpus.Lengths.Length is < 1 or > 8 || corpus.Lengths.Any(n => n is < 1 or > 128) || corpus.Cases.Any(c => c.Id.Length > 64 || c.Seed.Length == 0 || c.Seed.Any(ch => ch is < 'a' or > 'z'))) throw new InvalidDataException("Invalid synthetic corpus");
        Directory.CreateDirectory(output);
        File.WriteAllText(Path.Combine(output, ".isolated-baseline"), revision);
        var total = Stopwatch.StartNew(); string builder = Path.Combine(output, "builder"), shared = Path.Combine(output, "static");
        Directory.CreateDirectory(builder); Directory.CreateDirectory(shared);
        foreach (string file in fixtureFiles.Keys.Where(f => f.EndsWith(".yaml"))) { File.Copy(Path.Combine(fixture, file), Path.Combine(builder, file)); File.Copy(Path.Combine(fixture, file), Path.Combine(shared, file)); }
        NativeLibrary.SetDllImportResolver(typeof(NativeRime).Assembly, (name, _, _) => name == "baseline_rime" ? NativeLibrary.Load(engine) : IntPtr.Zero);
        using (NativeRime.Open(builder, builder, true))
        {
            NativeRime.RimeStartMaintenance(1); NativeRime.RimeJoinMaintenanceThread();
        }
        Directory.CreateDirectory(Path.Combine(shared, "build"));
        foreach (string file in Directory.GetFiles(Path.Combine(builder, "build"))) File.Copy(file, Path.Combine(shared, "build", Path.GetFileName(file)));
        var compiledBefore = Inventory(shared); var trials = new List<Trial>();
        int sequence = 0;
        foreach (var input in corpus.Cases)
        foreach (int length in corpus.Lengths)
        for (int repeat = -warmup; repeat < repeats; repeat++)
        {
            if (total.Elapsed.TotalMinutes > 8) throw new TimeoutException("Bounded baseline exceeded 8 minutes; partial run is not a successful result");
            string user = Path.Combine(output, "trials", (++sequence).ToString("D3")); Directory.CreateDirectory(Path.Combine(user, "build"));
            foreach (string file in Directory.GetFiles(Path.Combine(shared, "build"), "*.yaml")) File.Copy(file, Path.Combine(user, "build", Path.GetFileName(file)));
            if (Directory.GetDirectories(user, "*.userdb", SearchOption.AllDirectories).Length != 0) throw new InvalidDataException("Learning database was not empty");
            var samples = new List<Sample>();
            using (NativeRime.Open(shared, user))
            {
                UIntPtr session = NativeRime.RimeCreateSession(); if (session == UIntPtr.Zero) throw new InvalidOperationException("No isolated session");
                try
                {
                    if (NativeRime.RimeSelectSchema(session, "baseline") == 0) throw new InvalidOperationException("Fixture deployment failed");
                    NativeRime.RimeSetOption(session, "ascii_mode", 0);
                    for (int index = 0; index < length; index++)
                    {
                        var context = new NativeRime.Context { size = Marshal.SizeOf<NativeRime.Context>() - sizeof(int) };
                        long start = Stopwatch.GetTimestamp();
                        bool accepted = NativeRime.RimeProcessKey(session, input.Seed[index % input.Seed.Length], 0) != 0;
                        long core = Stopwatch.GetTimestamp() - start;
                        start = Stopwatch.GetTimestamp();
                        int result = NativeRime.RimeGetContext(session, ref context);
                        long contextTicks = Stopwatch.GetTimestamp() - start;
                        if (result == 0) throw new InvalidOperationException("No context; timing is invalid");
                        int page = context.menu.pageNo, highlight = context.menu.highlight;
                        start = Stopwatch.GetTimestamp(); Word[] words;
                        try
                        {
                            if (!accepted || context.menu.count is < 1 or > 1000) throw new InvalidOperationException("Missing candidates or rejected synthetic input; timing is invalid");
                            words = Enumerable.Range(0, context.menu.count).Select(n =>
                            {
                                var candidate = Marshal.PtrToStructure<NativeRime.Candidate>(IntPtr.Add(context.menu.candidates, n * Marshal.SizeOf<NativeRime.Candidate>()));
                                return new Word(Marshal.PtrToStringUTF8(candidate.text) ?? "", Marshal.PtrToStringUTF8(candidate.comment) ?? "");
                            }).ToArray();
                        }
                        finally { NativeRime.RimeFreeContext(ref context); }
                        double extractionMs = Ms(Stopwatch.GetTimestamp() - start);
                        string digest = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(JsonSerializer.Serialize(words))));
                        samples.Add(new(index + 1, accepted, Ms(core), Ms(contextTicks), extractionMs, page, highlight, words, digest));
                    }
                }
                finally { NativeRime.RimeClearComposition(session); NativeRime.RimeDestroySession(session); }
            }
            var trial = new Trial(input.Id, length, repeat, repeat < 0, samples.ToArray()); trials.Add(trial);
            await File.WriteAllTextAsync(Path.Combine(user, "samples.json"), JsonSerializer.Serialize(trial, Json));
        }
        var groups = trials.GroupBy(t => (t.CaseId, t.TargetLength)).Select(g =>
        {
            var measured = g.Where(t => !t.Warmup).ToArray(); var reference = measured[0].Samples;
            int differences = measured.Sum(t => t.Samples.Zip(reference).Count(pair => !SameSemantics(pair.First, pair.Second)));
            if (measured.Any(t => t.Samples.Length != reference.Length)) throw new InvalidDataException("Incomplete trial");
            return new { caseId = g.Key.CaseId, length = g.Key.TargetLength, repetitions = repeats, semanticDifferences = differences,
                core = Summary(measured.SelectMany(t => t.Samples.Select(s => s.CoreMs))), context = Summary(measured.SelectMany(t => t.Samples.Select(s => s.ContextMs))),
                extraction = Summary(measured.SelectMany(t => t.Samples.Select(s => s.ExtractionMs))), lastKeyCore = Summary(measured.Select(t => t.Samples[^1].CoreMs)),
                perRepetition = measured.Select(t => new { repetition = t.Repetition, core = Summary(t.Samples.Select(s => s.CoreMs)), context = Summary(t.Samples.Select(s => s.ContextMs)) }).ToArray() };
        }).ToArray();
        if (!compiledBefore.OrderBy(p => p.Key).SequenceEqual(Inventory(shared).OrderBy(p => p.Key)) || !fixtureFiles.OrderBy(p => p.Key).SequenceEqual(Inventory(fixture).OrderBy(p => p.Key))) throw new IOException("Static or source fixture changed during measurement");
        var process = Process.GetCurrentProcess();
        var report = new { schemaVersion = 1, sourceRevision = revision, engineSha256 = CoreHash, engineVersion = Marshal.PtrToStringUTF8(NativeRime.RimeGetVersion()),
            fixture = fixtureFiles, compiledFixture = compiledBefore, os = RuntimeInformation.OSDescription, runtime = RuntimeInformation.FrameworkDescription,
            architecture = RuntimeInformation.ProcessArchitecture.ToString(), cpu = Environment.GetEnvironmentVariable("PROCESSOR_IDENTIFIER"), logicalProcessors = Environment.ProcessorCount,
            stopwatchFrequency = Stopwatch.Frequency, warmupRepetitions = warmup, measuredRepetitions = repeats, quantile = "nearest-rank ceil(p*n)-1; pooled key samples within each case/target length; warmup excluded",
            order = "case, target length ascending, fresh-user warmup then fresh-user repetitions; process/OS caches remain warm", elapsedSeconds = total.Elapsed.TotalSeconds,
            peakWorkingSetBytes = process.PeakWorkingSet64, peakWorkingSetScope = "whole benchmark including deployment, warmup, report allocation; not per-key retained resources",
            stages = new { core_process_key = "RimeProcessKey only", get_context = "RimeGetContext only", candidate_extraction = "managed text extraction and RimeFreeContext; serialization/digest outside timers", context_response = "unavailable: no Weasel response serializer", ipc_roundtrip = "unavailable", layout = "unavailable", resource_prepare = "unavailable", paint_submit = "unavailable", present_complete = "unavailable" }, groups };
        await File.WriteAllTextAsync(Path.Combine(output, "report.json"), JsonSerializer.Serialize(report, Json));
        if (groups.Any(g => g.semanticDifferences != 0)) throw new InvalidDataException("Candidate semantics varied; report retained, performance comparison rejected");
        Console.WriteLine($"PASS {groups.Length} groups / {trials.Count} fresh learning trials; report.json; no HWND, IPC or daily data");
    }
    internal static void SelfTest()
    {
        void Check(bool ok) { if (!ok) throw new Exception("Baseline contract failed"); }
        Check(Percentile(new[] { 1d, 2, 3, 4 }, .5) == 2); Check(Percentile(Enumerable.Range(1, 20).Select(n => (double)n), .95) == 19);
        var a = new Sample(1, true, 1, 1, 1, 0, 0, [new("你好", "ni hao"), new("拟好", "ni hao")], "x");
        Check(SameSemantics(a, a with { CoreMs = 900 })); Check(!SameSemantics(a, a with { Candidates = a.Candidates.Reverse().ToArray() }));
        Check(!SameSemantics(a, a with { Candidates = [new("你好", "other")] })); Check(!SameSemantics(a, a with { Highlight = 1 }));
        Console.WriteLine("PASS 6 baseline statistics and candidate-order contracts");
    }
}
