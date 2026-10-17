using Shorekeeper.Core;

static class SkinLibraryTests
{
    public static async Task Run(string temp, Func<string, Action<SkinManifest>?, string> package, Action<bool, string> check, Action<Action, string> reject)
    {
        var store = new SkinStore(Path.Combine(temp, "library-operations"));
        var original = store.Import(package("library-original", null));
        store.Select(original, "watermelon", new SkinStyle { FontPoint = 23 });
        byte[] originalState = File.ReadAllBytes(store.StatePath);
        string manifest = Path.Combine(original.Directory, "manifest.json");
        byte[] originalManifest = File.ReadAllBytes(manifest);
        using (var pending = store.PrepareImport(package("library-cancel", m => m.Name = "编辑版")))
        {
            check(pending.SameVersionExists && pending.ExistingVersions.Count == 1, "import conflict is available before confirmation");
            reject(() => store.CommitImportAsync(pending, false).GetAwaiter().GetResult(), "duplicate commit requires explicit replacement");
        }
        check(originalState.SequenceEqual(File.ReadAllBytes(store.StatePath)) && originalManifest.SequenceEqual(File.ReadAllBytes(manifest)), "cancel preserves selection and installed package");
        check(!Directory.GetDirectories(store.Root, ".staging-*").Any(), "cancel releases staged package");
        int refreshes = 0;
        using (var pending = store.PrepareImport(package("library-replace", m => m.Name = "编辑版")))
        {
            var changed = await store.CommitImportAsync(pending, true, () => { refreshes++; return Task.CompletedTask; });
            check(changed.Manifest.Name == "编辑版" && store.List().Count == 1 && refreshes == 1, "replace same version updates package and refreshes active appearance");
            check(File.ReadAllBytes(Path.Combine(pending.BackupDirectory!, "package", "manifest.json")).SequenceEqual(originalManifest), "replace retains recoverable previous package");
        }
        check(store.Selection().VariantId == "watermelon" && store.Selection().StyleOverride == null, "replace preserves available variant and clears stale override");
        byte[] replacedManifest = File.ReadAllBytes(manifest), replacedState = File.ReadAllBytes(store.StatePath);
        using (var pending = store.PrepareImport(package("library-failed-replace", m => m.Name = "刷新失败")))
        {
            reject(() => store.CommitImportAsync(pending, true, () => throw new IOException("refresh failed")).GetAwaiter().GetResult(), "replacement refresh failure is reported");
        }
        check(replacedManifest.SequenceEqual(File.ReadAllBytes(manifest)) && replacedState.SequenceEqual(File.ReadAllBytes(store.StatePath)), "failed replacement restores exact package and selection");
        using (var pending = store.PrepareImport(package("library-fallback", m => m.Variants = m.Variants.Take(1).ToList())))
        {
            await store.CommitImportAsync(pending, true);
            check(store.Selection().VariantId == "plush", "removed active variant selects valid fallback");
        }
        using (var pending = store.PrepareImport(package("library-new-version", m => m.Version = "1.0.2")))
        {
            check(!pending.SameVersionExists && pending.ExistingVersions.Count == 1, "same identifier new version is distinguished from replacement");
            await store.CommitImportAsync(pending, false);
        }
        var next = store.List().Single(s => s.Manifest.Version == "1.0.2");
        byte[] active = File.ReadAllBytes(store.StatePath);
        string backup = await store.RemoveAsync(next, () => throw new Exception("inactive removal must not refresh"));
        check(store.List().Count == 1 && File.Exists(Path.Combine(backup, "package", "manifest.json")) && active.SequenceEqual(File.ReadAllBytes(store.StatePath)), "remove inactive version preserves active appearance and backup");
        var installed = store.List().Single();
        reject(() => store.RemoveAsync(installed, () => throw new IOException("refresh failed")).GetAwaiter().GetResult(), "active removal refresh failure is reported");
        check(Directory.Exists(installed.Directory) && active.SequenceEqual(File.ReadAllBytes(store.StatePath)), "failed removal restores package and selection");
        backup = await store.RemoveAsync(installed, () => { refreshes++; return Task.CompletedTask; });
        check(store.List().Count == 0 && store.Selection().SkinId == null && refreshes == 2, "active removal resets to native appearance");
        check(File.ReadAllBytes(Path.Combine(backup, "state.before.json")).SequenceEqual(active), "removal retains previous selection bytes");
        reject(() => store.RemoveAsync(new InstalledSkin(temp, original.Manifest)).GetAwaiter().GetResult(), "reject removal outside managed library");
        using var delayed = store.PrepareImport(package("library-delayed", m => m.Id = "race"));
        check(!delayed.SameVersionExists, "new import has no replacement authorization");
        new SkinStore(store.Root).Import(package("library-competing", m => { m.Id = "race"; m.Name = "先完成的导入"; }));
        reject(() => store.CommitImportAsync(delayed, false).GetAwaiter().GetResult(), "late collision cannot bypass replacement confirmation");
        check(store.List().Single().Manifest.Name == "先完成的导入", "late collision preserves competing import");
    }
}
