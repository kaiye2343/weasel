using System.Net;
using System.Text;
using Shorekeeper.Core;

static class DictionarySubscriptionTests
{
    static byte[] Dictionary(string body, string version = "1", string extraHeader = "") => Encoding.UTF8.GetBytes($"---\nname: original\nversion: '{version}'\n{extraHeader}...\n{body}");
    public static async Task Run(string temp, Action<bool, string> check, Action<Action, string> reject)
    {
        string root = Path.Combine(temp, "subscriptions"); Directory.CreateDirectory(root);
        var library = new DictionarySubscriptions(root);
        const string address = "https://example.invalid/words.dict.yaml";
        const string body = "守岸人\tshǒu àn rén\t100\n漂泊者\tpiāo bó zhě\t80\n";
        byte[] first = Dictionary(body);
        check(library.List().Count == 0 && !Directory.Exists(library.Library), "subscription page read creates no files");
        var import = library.PreviewImport("game", "游戏专有名词", address, first);
        check(library.List().Count == 0 && !Directory.Exists(library.Library), "subscription import preview writes nothing");
        import.Save(root);
        var saved = library.Read("game");
        check(saved.Revisions.Single().Entries == 2 && saved.Url == address, "subscription stores count version source and digest");
        check(!File.Exists(Path.Combine(root, "sub_game.dict.yaml")), "download alone does not activate dictionary");
        check(library.PreviewImport("game", saved.Name, address, first).Changes.Count == 0, "unchanged subscription is a no-op");
        reject(() => library.PreviewImport("../escape", "bad", address, first), "reject unsafe subscription id");
        reject(() => library.PreviewImport("bad", "bad", "http://example.invalid/dict", first), "reject HTTP subscription");
        reject(() => DictionarySubscriptions.ValidateUrl("https://user:secret@example.invalid/dict"), "reject credentials in subscription URL");
        reject(() => library.PreviewImport("game", "bad", "https://other.invalid/dict", first), "reject changing source under same subscription id");
        reject(() => library.PreviewImport("bad", "bad", "", "<html>not a dictionary</html>"u8.ToArray()), "reject downloaded HTML");
        reject(() => library.PreviewImport("bad", "bad", "", new byte[] { 0xff, 0xfe }), "reject non UTF8 dictionary");
        reject(() => library.PreviewImport("bad", "bad", "", Dictionary(body, extraHeader: "import_tables: [../private]\n")), "reject dictionary import references");
        reject(() => library.PreviewImport("bad", "bad", "", Dictionary(body, extraHeader: "encoder: {rules: []}\n")), "reject executable encoding configuration");
        reject(() => library.PreviewImport("bad", "bad", "", Dictionary("守岸人 shou an ren\n")), "reject words without tab delimiter");
        reject(() => library.PreviewImport("bad", "bad", "", Dictionary("守岸人\tshou an ren\tNaN\n")), "reject nonfinite word weights");
        reject(() => library.PreviewImport("bad", "bad", "", new byte[DictionarySubscriptions.MaxBytes + 1]), "bounded subscription payload");
        const string schema = "engine:\n  translators: [script_translator]\ntranslator:\n  dictionary: primary\n";
        File.WriteAllText(Path.Combine(root, "wanxiang_work.schema.yaml"), schema);
        File.WriteAllText(Path.Combine(root, "wanxiang_daily.schema.yaml"), schema);
        File.WriteAllText(Path.Combine(root, "primary.dict.yaml"), "---\nname: primary\nimport_tables: [chars]\n...\n");
        File.WriteAllBytes(Path.Combine(root, "chars.dict.yaml"), Dictionary("守\tshǒu\t1\n岸\tàn\t1\n人\trén\t1\n漂\tpiāo\t1\n泊\tbó\t1\n者\tzhě\t1\n"));
        string custom = Path.Combine(root, "wanxiang_work.custom.yaml");
        File.WriteAllText(custom, "patch:\n  translator/packs/+: [existing]\n  translator/enable_user_dict: true\n  menu/page_size: 6\n  unknown: keep\n");
        string daily = Path.Combine(root, "wanxiang_daily.custom.yaml");
        File.WriteAllText(daily, "patch:\n  unrelated: daily\n");
        string dailyBefore = File.ReadAllText(daily);
        Directory.CreateDirectory(Path.Combine(root, "wanxiang_work.userdb"));
        string sentinel = Path.Combine(root, "wanxiang_work.userdb", "sentinel"); File.WriteAllText(sentinel, "learning");
        var enable = library.PreviewEnable("game", "wanxiang_work", true);
        check(!library.IsEnabled("game", "wanxiang_work"), "enable preview leaves scheme unchanged");
        string enableBackup = enable.Save(root);
        check(library.IsEnabled("game", "wanxiang_work") && !library.IsEnabled("game", "wanxiang_daily"), "enable targets one scenario");
        check(File.ReadAllText(custom).Contains("existing") && File.ReadAllText(custom).Contains("unknown: keep") && File.ReadAllText(custom).Contains("menu/page_size: 6"), "subscription patch preserves existing packs and unrelated settings");
        check(File.ReadAllText(daily) == dailyBefore && File.ReadAllText(sentinel) == "learning", "subscription does not touch other scenario or learning database");
        check(File.ReadAllText(Path.Combine(root, "sub_game.dict.yaml")).Contains("name: sub_game"), "managed dictionary has unique name matching its filename");
        check(library.EnabledSchemas("game").SequenceEqual(new[] { "wanxiang_work" }), "subscription shows saved scheme scope");
        reject(() => library.PreviewRemove("game"), "cannot remove referenced subscription");
        var plain = library.PreviewImport("plain", "无调", "", Dictionary("守岸人\tshou an ren\t1\n")); plain.Save(root);
        reject(() => library.PreviewEnable("plain", "wanxiang_work", true), "refuse toneless dictionary on tone-coded scheme");
        File.WriteAllText(Path.Combine(root, "shape.schema.yaml"), "engine:\n  translators: [table_translator]\ntranslator:\n  dictionary: primary\n");
        reject(() => library.PreviewEnable("game", "shape", true), "unsupported translator remains downloaded without invalid toggle");
        File.WriteAllText(Path.Combine(root, "complex.schema.yaml"), schema);
        File.WriteAllText(Path.Combine(root, "complex.custom.yaml"), "patch:\n  translator:\n    packs: [other]\n");
        reject(() => library.PreviewEnable("game", "complex", true), "reject complex patch without replacing it");
        byte[] second = Dictionary(body + "守岸\tshǒu àn\t50\n", "2");
        reject(() => library.PreviewImport("game", saved.Name, address, Dictionary(body + "生造\tunmapped code\t1\n", "bad")), "update checks compatibility for already enabled schemas");
        string indexFile = Path.Combine(library.Library, "game", "subscription.json");
        string installedBeforeFailure = File.ReadAllText(Path.Combine(root, "sub_game.dict.yaml"));
        var failedSave = library.PreviewImport("game", saved.Name, address, second);
        File.SetAttributes(indexFile, FileAttributes.ReadOnly);
        try { reject(() => failedSave.Save(root), "subscription transaction handles registry write failure"); }
        finally { File.SetAttributes(indexFile, FileAttributes.Normal); }
        check(File.ReadAllText(Path.Combine(root, "sub_game.dict.yaml")) == installedBeforeFailure && library.Read("game").CurrentHash == saved.CurrentHash, "failed save rolls back installed dictionary and current revision");
        string updateBackup = library.PreviewImport("game", saved.Name, address, second).Save(root);
        check(library.Read("game").Revisions.Count == 2 && File.ReadAllText(Path.Combine(root, "sub_game.dict.yaml")).Contains("守岸\t"), "update preserves previous revision and updates enabled file");
        library.PreviewRollback("game", saved.CurrentHash).Save(root);
        check(library.Read("game").CurrentHash == saved.CurrentHash && !File.ReadAllText(Path.Combine(root, "sub_game.dict.yaml")).Contains("守岸\t"), "rollback restores previous dictionary while preserving patch");
        string installed = Path.Combine(root, "sub_game.dict.yaml"), owned = File.ReadAllText(installed);
        File.AppendAllText(installed, "个人词\tgè rén cí\t100\n");
        reject(() => library.PreviewImport("game", saved.Name, address, second), "manual word edits block update instead of being overwritten");
        check(File.ReadAllText(installed).Contains("个人词"), "failed update preserves personal edits"); File.WriteAllText(installed, owned);
        File.AppendAllText(custom, "# later change\n");
        // Import watches the managed file and registry; the activation operation also watches its scheme patch.
        var racedEnable = library.PreviewEnable("game", "wanxiang_work", false);
        File.AppendAllText(custom, "# newest change\n");
        reject(() => racedEnable.Save(root), "enable or disable refuses edits since review");
        string currentCache = Path.Combine(library.Library, "game", saved.CurrentHash + ".dict.yaml");
        string intact = File.ReadAllText(currentCache); File.AppendAllText(currentCache, "额外\té wài\t1\n");
        reject(() => library.PreviewRollback("game", library.Read("game").Revisions.First(r => r.Hash != saved.CurrentHash).Hash), "tampered current cache blocks owned file replacement");
        File.WriteAllText(currentCache, intact);
        library.PreviewEnable("game", "wanxiang_work", false).Save(root);
        check(!library.IsEnabled("game", "wanxiang_work") && File.ReadAllText(custom).Contains("existing"), "disable removes only managed pack");
        string removeBackup = library.PreviewRemove("game").Save(root);
        check(library.List().All(s => s.Id != "game") && File.Exists(currentCache), "remove subscription preserves historical downloads");
        RimeSettings.PreviewRestore(root, removeBackup).Save(root);
        check(library.Read("game").CurrentHash == saved.CurrentHash && File.Exists(installed), "subscription removal can be restored through existing backup mechanism");
        string exported = Path.Combine(temp, "subscription-export.dict.yaml"); library.Export("game", exported);
        check(new DictionarySubscriptions(Path.Combine(temp, "exported-dictionaries")).PreviewImport("reimport", "再导入", "", File.ReadAllBytes(exported)).Changes.Count == 2, "subscription export reimports");
        check(File.Exists(Path.Combine(updateBackup, "changes.json")) && File.Exists(Path.Combine(enableBackup, "restore-index.json")), "subscription operations include backup and change records");

        var handler = new DownloadHandler(first); using var client = new HttpClient(handler);
        byte[] downloaded = await DictionarySubscriptions.Download(client, address);
        check(downloaded.SequenceEqual(first) && handler.Calls == 1, "explicit download retrieves dictionary with no background requests");
        handler.Redirect = new Uri("https://cdn.example.invalid/words.dict.yaml");
        check((await DictionarySubscriptions.Download(client, address)).SequenceEqual(first), "HTTPS release redirect supported");
        handler.Redirect = new Uri("http://cdn.example.invalid/words.dict.yaml");
        await RejectAsync(() => DictionarySubscriptions.Download(client, address), check, "reject redirect downgrade before requesting target");
        handler.Redirect = null; handler.Fail = true;
        string indexBefore = File.ReadAllText(Path.Combine(library.Library, "game", "subscription.json"));
        await RejectAsync(() => DictionarySubscriptions.Download(client, address), check, "network failure preserves saved subscription");
        check(File.ReadAllText(Path.Combine(library.Library, "game", "subscription.json")) == indexBefore, "network failure does not mutate local state");
        handler.Fail = false; handler.Oversized = true;
        await RejectAsync(() => DictionarySubscriptions.Download(client, address), check, "download checks advertised size before reading");
    }
    static async Task RejectAsync(Func<Task> action, Action<bool, string> check, string name)
    {
        try { await action(); } catch (Exception e) when (e is IOException or InvalidDataException) { check(true, name); return; }
        throw new Exception("Accepted invalid subscription operation: " + name);
    }
    sealed class DownloadHandler(byte[] bytes) : HttpMessageHandler
    {
        public int Calls;
        public Uri? Redirect;
        public bool Fail, Oversized;
        protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken ct)
        {
            Calls++;
            if (Redirect != null && request.RequestUri!.Host == "example.invalid")
            {
                var redirect = new HttpResponseMessage(HttpStatusCode.Found); redirect.Headers.Location = Redirect; return Task.FromResult(redirect);
            }
            var response = new HttpResponseMessage(Fail ? HttpStatusCode.ServiceUnavailable : HttpStatusCode.OK) { Content = new ByteArrayContent(bytes) };
            if (Oversized) response.Content.Headers.ContentLength = DictionarySubscriptions.MaxBytes + 1L;
            return Task.FromResult(response);
        }
    }
}
