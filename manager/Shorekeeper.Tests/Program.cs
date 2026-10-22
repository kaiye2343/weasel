using System.IO.Compression;
using System.Text.Json;
using Shorekeeper.Core;

string project = args.Length > 0 ? Path.GetFullPath(args[0]) : Path.GetFullPath(".");
string temp = Path.Combine(project, ".tools", "tests-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(temp);
int passed = 0;
void Check(bool ok, string name) { if (!ok) throw new Exception(name); Console.WriteLine("PASS " + name); passed++; }
void Reject(Action action, string name) { try { action(); } catch (Exception e) when (e is InvalidDataException or IOException or OverflowException or JsonException or ArgumentException or UnauthorizedAccessException) { Check(true, name); return; } throw new Exception("Accepted invalid input: " + name); }
string Package(string name, string? extra = null, Action<SkinManifest>? edit = null)
{
    string source = Path.Combine(project, "skins", "shorekeeper");
    string zipPath = Path.Combine(temp, name + ".rimeskin");
    using var zip = ZipFile.Open(zipPath, ZipArchiveMode.Create);
    var manifest = SkinJson.Read<SkinManifest>(Path.Combine(source, "manifest.json"));
    edit?.Invoke(manifest);
    using (var writer = new StreamWriter(zip.CreateEntry("manifest.json").Open())) writer.Write(JsonSerializer.Serialize(manifest, new JsonSerializerOptions(SkinJson.Options) { RespectNullableAnnotations = false }));
    foreach (string png in Directory.GetFiles(source, "*.png")) zip.CreateEntryFromFile(png, Path.GetFileName(png));
    if (extra != null) { using var writer = new StreamWriter(zip.CreateEntry(extra).Open()); writer.Write("invalid"); }
    return zipPath;
}
try
{
    SkinDiagnosticsTests.Run(temp, Check);
    SettingsTests.Run(temp, Check, Reject);
    await SkinLibraryTests.Run(temp, (name, edit) => Package(name, edit: edit), Check, Reject);
    var store = new SkinStore(Path.Combine(temp, "store"));
    var skin = store.Import(Package("valid"));
    Check(store.List().Count == 1, "import valid package");
    store.Select(skin, "plush");
    byte[] selected = File.ReadAllBytes(store.StatePath);
    Reject(() => store.Import(Package("traversal", "../escape.png")), "reject traversal");
    Reject(() => store.Import(Package("backslash", "assets\\escape.png")), "reject Windows path");
    Reject(() => store.Import(Package("exe", "payload.exe")), "reject executable");
    Reject(() => store.Import(Package("future", edit: m => m.SchemaVersion = 2)), "reject unknown schema");
    Reject(() => store.Import(Package("layers", edit: m => m.Variants[0].Layers = Enumerable.Repeat(m.Variants[0].Layers[0], 9).ToList())), "reject excess layers");
    Reject(() => store.Import(Package("dimensions", edit: m => m.Variants[0].Layers[0].Width = 10000)), "reject dimensions");
    Reject(() => store.Import(Package("duplicate")), "preserve existing version");
    Check(selected.SequenceEqual(File.ReadAllBytes(store.StatePath)), "failed imports preserve selection");
    Check(!Directory.GetDirectories(store.Root, ".staging-*").Any(), "failed import cleans owned staging");
    store.Import(Package("new-version", edit: m => m.Version = "1.0.1"));
    Check(store.List().Count == 2, "versions coexist");
    store.Select(skin, "watermelon"); Check(store.Selection().VariantId == "watermelon", "variant switch");
    store.Reset(); Check(store.Selection().SkinId == null, "reset selection");
    var preferences = new DisplayPreferencesStore(store.Root);
    store.Select(skin, "plush", new SkinStyle { AnnotationPosition = "hidden", FontPoint = 21 });
    preferences.Save(new() { AnnotationPosition = "above", FontMode = "skin" });
    Check(preferences.EffectiveStyle(store, skin, skin.Manifest.Variants[0]).AnnotationPosition == "above" && preferences.EffectiveStyle(store, skin, skin.Manifest.Variants[0]).FontPoint == 21, "display preference overrides annotation and preserves legacy style");
    preferences.Save(new() { AnnotationPosition = "skin", FontMode = "skin" });
    Check(preferences.EffectiveStyle(store, skin, skin.Manifest.Variants[0]).AnnotationPosition == "hidden", "follow skin preserves selected legacy override");
    store.Reset(); Check(preferences.Read().AnnotationPosition == "skin", "skin reset does not delete independent display preference");
    var pure = store.Import(Package("pure", edit: m => { m.Id = "neutral"; m.Name = "纯配色"; m.Variants = m.Variants.Take(1).ToList(); m.Variants[0].Layers.Clear(); m.Variants[0].Style.Frame = null; }));
    store.Select(pure, pure.Manifest.Variants[0].Id); Check(store.List().Count == 3 && pure.Manifest.Variants.Count == 1, "generic pure color package and arbitrary variant count");
    store.Select(skin, "plush"); Check(store.Selection().SkinVersion == "1.0.0", "rollback to installed earlier package version");
    string export = Path.Combine(temp, "export.rimeskin"); store.Export(skin, export);
    Check(new SkinStore(Path.Combine(temp, "second")).Import(export).Manifest.Id == "shorekeeper", "export reimports");
    Reject(() => SkinStore.ResolveAsset(temp, "NUL.png"), "reject reserved device path");
    Reject(() => store.Import(Package("null-style", edit: m => m.Variants[0].Style = null!)), "reject null required style");
    var badStyle = new SkinStyle { Frame = new SkinFrame { Path = "wordmark-art.png", Slice = [2048, 0, 2048, 0] } };
    Reject(() => store.Select(skin, "plush", badStyle), "reject invalid override frame");

    string profilesRoot = Path.Combine(temp, "profiles"); Directory.CreateDirectory(profilesRoot);
    File.WriteAllText(Path.Combine(profilesRoot, "wanxiang.schema.yaml"), "schema:\n  schema_id: wanxiang\n  name: 万象\n  dependencies: [wanxiang_phrase]\nswitches:\n  - name: ascii_mode\n  - options: [s2s, s2t, s2hk, s2tw]\n  - name: context_reorder\ntranslator:\n  dictionary: wanxiang\n");
    File.WriteAllText(Path.Combine(profilesRoot, "default.custom.yaml"), "patch:\n  schema_list:\n    - schema: existing\n  menu/page_size: 7\n");
    var profiles = new RimeProfiles(profilesRoot); profiles.Create("work"); profiles.Create("daily");
    string workCustom = Path.Combine(profilesRoot, "wanxiang_work.custom.yaml");
    string dailyCustom = Path.Combine(profilesRoot, "wanxiang_daily.custom.yaml");
    string workBefore = File.ReadAllText(workCustom), dailyBefore = File.ReadAllText(dailyCustom);
    Check(workBefore.Contains("wanxiang_work") && dailyBefore.Contains("wanxiang_daily") && workBefore.Contains("zc_work") && dailyBefore.Contains("zc_daily"), "distinct learning database names");
    Check(workBefore.Contains("enable_auto_phrase: true"), "auto phrase learning enabled");
    var settings = profiles.Load("work"); settings.FuzzyGroups.Add("n_l"); settings.Shortcuts.Add("sk", "守岸人"); settings.PinyinMode = "自然码";
    string backup = profiles.Apply("work", settings);
    Check(File.ReadAllText(dailyCustom) == dailyBefore, "work settings do not change daily");
    var loaded = profiles.Load("work"); Check(loaded.PinyinMode == "自然码" && loaded.FuzzyGroups.SequenceEqual(new[] { "n_l" }) && loaded.Shortcuts["sk"] == "守岸人", "input settings round trip");
    Check(File.ReadAllText(Path.Combine(backup, "wanxiang_work.custom.yaml")) == workBefore, "original configuration backed up");
    Check(File.ReadAllText(Path.Combine(profilesRoot, "default.custom.yaml")).Contains("menu/page_size: 7"), "unrelated default patch preserved");
    string baseCustom = Path.Combine(profilesRoot, "wanxiang.custom.yaml");
    File.WriteAllText(baseCustom, "patch:\n  translator/enable_correction: false\n  speller/algebra/__patch: [wanxiang_algebra:/base/全拼, custom_extra]\n  unrelated: keep-base\n");
    var baseSettings = profiles.LoadScheme("wanxiang"); baseSettings.EnableCorrection = true;
    var basePlan = new ConfigurationPlan(profiles.PreviewScheme("wanxiang", baseSettings)); basePlan.Save(profilesRoot);
    string baseAfter = File.ReadAllText(baseCustom);
    Check(baseAfter.Contains("custom_extra") && baseAfter.Contains("unrelated: keep-base") && !baseAfter.Contains("switches/@"), "Wanxiang base changes only edited option");
    baseSettings.PinyinMode = "小鹤双拼"; new ConfigurationPlan(profiles.PreviewScheme("wanxiang", baseSettings)).Save(profilesRoot);
    Check(File.ReadAllText(baseCustom).Contains("custom_extra") && profiles.LoadScheme("wanxiang").PinyinMode == "小鹤双拼", "Wanxiang mode preserves unrelated algebra references");
    string inheritedRoot = Path.Combine(temp, "inherited-scenes"); Directory.CreateDirectory(inheritedRoot);
    File.Copy(Path.Combine(profilesRoot, "wanxiang.schema.yaml"), Path.Combine(inheritedRoot, "wanxiang.schema.yaml"));
    File.WriteAllText(Path.Combine(inheritedRoot, "default.yaml"), "schema_list:\n  - schema: wanxiang\n  - schema: plain\n");
    new RimeProfiles(inheritedRoot).Create("work");
    Check(new RimeSettings(inheritedRoot).Schemas().Where(s => s.Enabled).Select(s => s.Id).SequenceEqual(new[] { "wanxiang", "plain", "wanxiang_work" }), "scene creation preserves inherited enabled schemas");
    string import = Path.Combine(temp, "words.tsv"); File.WriteAllText(import, "守岸人\tshou an ren\t100\n"); profiles.ImportDictionary("work", import); profiles.ImportDictionary("work", import);
    Check(File.ReadAllLines(Path.Combine(profilesRoot, "work.dict.yaml")).Count(l => l.StartsWith("守岸人\t")) == 1, "word import deduplicates");
    Reject(() => profiles.WriteBatchBackedUp(new Dictionary<string, string> { [workCustom] = "changed", [Path.Combine(temp, "escape")] = "bad" }), "reject batch outside profile root");
    Check(File.ReadAllText(workCustom).Contains("自然码"), "invalid batch leaves original files");
    string blocked = Path.Combine(profilesRoot, "blocked"); File.WriteAllText(blocked, "file");
    Reject(() => profiles.WriteBatchBackedUp(new Dictionary<string, string> { [workCustom] = "changed", [Path.Combine(blocked, "child.yaml")] = "bad" }), "batch staging failure");
    Check(File.ReadAllText(workCustom).Contains("自然码"), "staging failure commits no changes");
    string workSaved = File.ReadAllText(workCustom);
    File.SetAttributes(dailyCustom, FileAttributes.ReadOnly);
    try { Reject(() => profiles.WriteBatchBackedUp(new Dictionary<string, string> { [workCustom] = "changed", [dailyCustom] = "blocked" }), "second commit failure rolls back batch"); }
    finally { File.SetAttributes(dailyCustom, FileAttributes.Normal); }
    Check(File.ReadAllText(workCustom) == workSaved && File.ReadAllText(dailyCustom) == dailyBefore, "committed file restored after batch failure");
    File.WriteAllText(Path.Combine(profilesRoot, "installation.yaml"), "installation_id: windows-a\n");
    string activeDb = Path.Combine(profilesRoot, "wanxiang_work.userdb"); Directory.CreateDirectory(activeDb); File.WriteAllText(Path.Combine(activeDb, "sentinel"), "active-db");
    string android = Path.Combine(temp, "android.zip"); profiles.ExportAndroid(android);
    using (var zip = ZipFile.OpenRead(android)) Check(!zip.Entries.Any(e => e.FullName.Contains("installation.yaml") || e.FullName.Contains(".userdb/")) && zip.Entries.Any(e => e.FullName.Contains("shortcuts_work.schema.yaml")), "Android excludes device identity and active databases");

    var dav = new MemoryDav(); var runner = new FakeRime(profilesRoot);
    var remoteIndex = new SyncIndex { Generation = "g1", Files = new() { ["wanxiang_work.userdb.txt"] = MemoryDav.Hash("phone-learning") } };
    dav.Seed("rime-sync/phone-b/latest.json", JsonSerializer.SerializeToUtf8Bytes(remoteIndex, SkinJson.Options));
    dav.Seed("rime-sync/phone-b/g1/wanxiang_work.userdb.txt", System.Text.Encoding.UTF8.GetBytes("phone-learning"));
    var sync = new SnapshotSync(dav, runner); var result = await sync.Run(profilesRoot);
    Check(runner.Calls == 1 && result.Downloaded == 1 && runner.SawRemote, "download precedes official merge");
    Check(File.ReadAllText(Path.Combine(activeDb, "sentinel")) == "active-db", "sync never writes active userdb");
    Check(dav.Files.Keys.Any(k => k.StartsWith("rime-sync/windows-a/") && k.EndsWith("wanxiang_work.userdb.txt")), "own snapshots uploaded");
    byte[] pointer = dav.Files["rime-sync/windows-a/latest.json"].Content;
    dav.FailUploads = true;
    try { await sync.Run(profilesRoot); throw new Exception("Expected upload failure"); } catch (IOException) { }
    Check(pointer.SequenceEqual(dav.Files["rime-sync/windows-a/latest.json"].Content), "interrupted upload preserves published generation");
    dav.FailUploads = false; dav.Conflict = true;
    try { await sync.Run(profilesRoot); throw new Exception("Expected ETag conflict"); } catch (IOException) { }
    Check(pointer.SequenceEqual(dav.Files["rime-sync/windows-a/latest.json"].Content), "ETag conflict preserves published generation");
    dav.Conflict = false;
    dav.Seed("rime-sync/phone-b/g1/wanxiang_work.userdb.txt", System.Text.Encoding.UTF8.GetBytes("tampered")); int calls = runner.Calls;
    try { await sync.Run(profilesRoot); throw new Exception("Expected hash failure"); } catch (InvalidDataException) { }
    Check(runner.Calls == calls && File.ReadAllText(Path.Combine(profilesRoot, "sync", "phone-b", "wanxiang_work.userdb.txt")) == "phone-learning", "bad hash leaves local snapshots and engine untouched");
    Reject(() => new HttpWebDav(new Uri("http://example.invalid/dav"), "u", "secret"), "HTTPS required");
    string annotationSchema = Path.Combine(profilesRoot, "wanxiang_work.schema.yaml");
    File.WriteAllText(annotationSchema, File.ReadAllText(annotationSchema).Replace("\r\n", "\n").Replace("\n...\n", "\n") + "\nsuper_comment:\n  candidate_length: 2\n");
    var annotated = profiles.Load("work");
    Check(annotated.AnnotationLength == 2, "read Wanxiang annotation length restriction");
    annotated.AnnotationLength = 64;
    string dailyAnnotationBefore = File.ReadAllText(dailyCustom);
    new ConfigurationPlan(profiles.PreviewScheme("wanxiang_work", annotated)).Save(profilesRoot);
    Check(profiles.Load("work").AnnotationLength == 64 && File.ReadAllText(dailyCustom) == dailyAnnotationBefore, "long annotation patch preserves other scenario");
    annotated.AnnotationLength = 65;
    Reject(() => profiles.PreviewScheme("wanxiang_work", annotated), "annotation length range validation");
    var plainOnly = store.Import(Package("plain-only", edit: m => { m.Id = "plain-only"; m.SchemaVersion = 2; m.MinEngineVersion = "1.1"; m.AnnotationSupport = "none"; }));
    preferences.Save(new() { AnnotationPosition = "above" });
    Check(preferences.EffectiveStyle(store, plainOnly, plainOnly.Manifest.Variants[0]).AnnotationPosition == "hidden", "plain-only package overrides global annotation preference");
    var bothLayouts = store.Import(Package("both-layouts", edit: m => { m.Id = "both-layouts"; m.SchemaVersion = 2; m.MinEngineVersion = "1.1"; m.AnnotationSupport = "both"; }));
    Check(preferences.EffectiveStyle(store, bothLayouts, bothLayouts.Manifest.Variants[0]).AnnotationPosition == "above", "both-layout package follows display preference");
    Reject(() => store.Import(Package("missing-capability", edit: m => { m.SchemaVersion = 2; m.MinEngineVersion = "1.1"; })), "v2 requires annotation capability");
    Reject(() => store.Import(Package("wrong-capability", edit: m => { m.SchemaVersion = 2; m.MinEngineVersion = "1.1"; m.AnnotationSupport = "guess"; })), "reject unknown annotation capability");
    Reject(() => store.Import(Package("v1-capability", edit: m => m.AnnotationSupport = "none")), "v1 does not silently ignore capability");
    string plainExport = Path.Combine(temp, "plain-export.rimeskin"); store.Export(plainOnly, plainExport);
    Check(new SkinStore(Path.Combine(temp, "plain-reimport")).Import(plainExport).Manifest.AnnotationSupport == "none", "capability survives export and reimport");
    foreach (string folder in new[] { "studio-demo", "studio-plain" })
    {
        var sampleManifest = SkinStore.ValidateDirectory(Path.Combine(project, "skins", folder));
        Check(sampleManifest.SchemaVersion == 2, "native PNG validation of designer sample " + folder);
    }
    string browserExport = Path.Combine(project, ".tools", "designer-browser", "browser-export.rimeskin");
    if (File.Exists(browserExport))
    {
        var webSkin = new SkinStore(Path.Combine(temp, "browser-export")).Import(browserExport);
        Check(webSkin.Manifest.AnnotationSupport == "none" && webSkin.Manifest.Variants.Count == 2, "real browser export imports with production validator");
    }
    await DisplayTests.Run(temp, Check, Reject);
    Console.WriteLine($"{passed} tests passed.");
}


finally
{
    string safeRoot = Path.GetFullPath(Path.Combine(project, ".tools")) + Path.DirectorySeparatorChar;
    if (Path.GetFullPath(temp).StartsWith(safeRoot, StringComparison.OrdinalIgnoreCase))
    {
        foreach (string file in Directory.GetFiles(temp, "*", SearchOption.AllDirectories)) File.SetAttributes(file, FileAttributes.Normal);
        Directory.Delete(temp, true);
    }
}

sealed class MemoryDav : IWebDav
{
    public Dictionary<string, DavFile> Files { get; } = [];
    public bool FailUploads, Conflict;
    int version;
    public static string Hash(string text) => Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(System.Text.Encoding.UTF8.GetBytes(text)));
    public void Seed(string path, byte[] bytes) => Files[path] = new(bytes, "\"" + ++version + "\"");
    public Task<IReadOnlyList<string>> Directories(string path, CancellationToken ct) => Task.FromResult<IReadOnlyList<string>>(Files.Keys.Where(k => k.StartsWith(path + "/")).Select(k => k[(path.Length + 1)..].Split('/')[0]).Distinct().ToArray());
    public Task<DavFile?> Get(string path, CancellationToken ct) => Task.FromResult(Files.GetValueOrDefault(path));
    public Task MakeDirectory(string path, CancellationToken ct) => Task.CompletedTask;
    public Task Put(string path, byte[] bytes, string? etag, bool createOnly, CancellationToken ct)
    {
        if (FailUploads || (Conflict && path.EndsWith("latest.json")) || (createOnly && Files.ContainsKey(path)) || (!createOnly && etag != Files.GetValueOrDefault(path)?.ETag)) throw new IOException("Simulated failure/conflict");
        Seed(path, bytes); return Task.CompletedTask;
    }
}
sealed class FakeRime(string root) : IRimeRunner
{
    public int Calls; public bool SawRemote;
    public Task RunAsync(string operation, CancellationToken ct = default)
    {
        if (operation != "sync") throw new Exception("Unexpected operation"); Calls++;
        SawRemote = File.Exists(Path.Combine(root, "sync", "phone-b", "wanxiang_work.userdb.txt"));
        string own = Path.Combine(root, "sync", "windows-a"); Directory.CreateDirectory(own);
        File.WriteAllText(Path.Combine(own, "wanxiang_work.userdb.txt"), "simulated-official-snapshot");
        return Task.CompletedTask;
    }
}
