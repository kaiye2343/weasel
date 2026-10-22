using Shorekeeper.Core;

static class DisplayTests
{
    public static async Task Run(string temp, Action<bool, string> check, Action<Action, string> reject)
    {
        var store = new DisplayPreferencesStore(Path.Combine(temp, "display-v2"));
        var defaults = store.Read();
        check(defaults.SchemaVersion == 2 && defaults.FontPoint == 12 && defaults.CommentFontPoint == 10 && defaults.Density == "compact", "compact independent display defaults");
        check(defaults.AnnotationEnabled && defaults.AnnotationPosition == "above" && defaults.EmptyAnnotationMode == "collapse", "annotation row defaults to enabled and auto collapse");
        reject(() => store.Save(new() { FontPoint = 0 }), "invalid display font rejected");
        reject(() => store.Save(new() { Density = "unknown" }), "unknown density rejected");
        check(defaults.CandidatePlacement == "auto" && defaults.CandidateGap == 6 && defaults.CandidateOffsetX == 0 && defaults.AvoidDecorations, "safe placement defaults for legacy files");
        reject(() => store.Save(new() { CandidatePlacement = "screen" }), "unknown candidate placement rejected");
        reject(() => store.Save(new() { CandidateOffsetX = 257 }), "candidate offset bounded");
        reject(() => store.Save(new() { CandidateGap = -1 }), "negative candidate gap rejected");
        reject(() => store.Save(new() { CandidateGap = 129 }), "candidate gap bounded");
        Directory.CreateDirectory(Path.GetDirectoryName(store.PathName)!);
        string legacy = "{\"schemaVersion\":1,\"annotationPosition\":\"hidden\"}\n";
        File.WriteAllText(store.PathName, legacy);
        var migrated = store.Read();
        check(!migrated.AnnotationEnabled && migrated.FontMode == "skin" && migrated.Density == "skin", "legacy preferences preserve hidden and skin sizing");
        check(File.ReadAllText(store.PathName) == legacy, "reading legacy preferences never writes them");
        check(migrated.CandidateGap == 6 && migrated.AvoidDecorations, "legacy preferences receive safe placement without rewriting");
        try { await store.SaveAndApplyAsync(new(), () => throw new IOException("refresh rejected")); throw new Exception("Expected display failure"); } catch (IOException) { }
        check(File.ReadAllText(store.PathName) == legacy, "failed refresh restores exact legacy file");
        File.Delete(store.PathName);
        try { await store.SaveAndApplyAsync(new(), () => throw new IOException("refresh rejected")); throw new Exception("Expected display failure"); } catch (IOException) { }
        check(!File.Exists(store.PathName), "failed first refresh restores missing preference file");
        bool refreshed = false;
        await store.SaveAndApplyAsync(new() { AnnotationEnabled = false, EmptyAnnotationMode = "reserve", Density = "standard" }, () => { refreshed = true; return Task.CompletedTask; });
        check(refreshed && !store.Read().AnnotationEnabled && store.Read().EmptyAnnotationMode == "reserve" && store.Read().Density == "standard", "display modes round trip and invoke only appearance refresh");
        await store.SaveAndApplyAsync(new() { CandidatePlacement = "above", CandidateOffsetX = -20, CandidateGap = 18, AvoidDecorations = false }, () => Task.CompletedTask);
        var placed = store.Read();
        check(placed.CandidatePlacement == "above" && placed.CandidateOffsetX == -20 && placed.CandidateGap == 18 && !placed.AvoidDecorations, "placement settings round trip independently of skin style");
        string previousPlacement = File.ReadAllText(store.PathName);
        try { await store.SaveAndApplyAsync(new(), () => throw new IOException("refresh rejected")); throw new Exception("Expected display failure"); } catch (IOException) { }
        check(File.ReadAllText(store.PathName) == previousPlacement, "placement refresh failure restores exact previous preferences");
        reject(() => SkinStore.ValidateStyle(new() { AnnotationSeparator = new() { Thickness = 0 } }), "invalid separator thickness rejected");
        reject(() => SkinStore.ValidateStyle(new() { AnnotationSeparator = new() { Color = "black" } }), "invalid separator color rejected");
    }
}
