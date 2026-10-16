using System.Text.Json;

namespace Shorekeeper.Core;

public sealed class DisplayPreferences
{
    public int SchemaVersion { get; set; } = 2;
    public bool AnnotationEnabled { get; set; } = true;
    public string AnnotationPosition { get; set; } = "above";
    public string EmptyAnnotationMode { get; set; } = "collapse";
    public string FontMode { get; set; } = "user";
    public int FontPoint { get; set; } = 12;
    public int CommentFontPoint { get; set; } = 10;
    public string Density { get; set; } = "compact";
}
public sealed class DisplayPreferencesStore(string root)
{
    public string PathName => Path.Combine(root, "display.json");
    public DisplayPreferences Read()
    {
        var value = File.Exists(PathName) ? SkinJson.Read<DisplayPreferences>(PathName) : new();
        if (value.SchemaVersion == 1)
        {
            value.SchemaVersion = 2; value.FontMode = "skin"; value.Density = "skin";
            value.AnnotationEnabled = value.AnnotationPosition != "hidden";
            if (value.AnnotationPosition == "hidden") value.AnnotationPosition = "above";
        }
        Validate(value); return value;
    }
    static void Validate(DisplayPreferences value)
    {
        if (value.SchemaVersion != 2 || value.AnnotationPosition is not ("skin" or "above" or "beside") ||
            value.EmptyAnnotationMode is not ("collapse" or "reserve") || value.FontMode is not ("user" or "skin") ||
            value.Density is not ("compact" or "standard" or "skin") || value.FontPoint is < 8 or > 48 || value.CommentFontPoint is < 8 or > 32)
            throw new InvalidDataException("候选显示偏好无效。");
    }
    public void Save(DisplayPreferences value) { Validate(value); SkinJson.WriteAtomic(PathName, value); }
    public async Task SaveAndApplyAsync(DisplayPreferences value, Func<Task> apply)
    {
        // Preserve the exact old file, including legacy/unknown layout formatting, on refresh failure.
        byte[]? previous = File.Exists(PathName) ? File.ReadAllBytes(PathName) : null;
        Save(value);
        try { await apply(); }
        catch
        {
            if (previous == null) File.Delete(PathName);
            else { string temp = PathName + ".rollback.tmp"; File.WriteAllBytes(temp, previous); File.Move(temp, PathName, true); }
            throw;
        }
    }
    public SkinStyle EffectiveStyle(SkinStore store, InstalledSkin skin, SkinVariant variant)
    {
        var selected = store.Selection();
        var original = selected.SkinId == skin.Manifest.Id && selected.SkinVersion == skin.Manifest.Version && selected.VariantId == variant.Id && selected.StyleOverride != null ? selected.StyleOverride : variant.Style;
        var result = JsonSerializer.Deserialize<SkinStyle>(JsonSerializer.Serialize(original, SkinJson.Options), SkinJson.Options)!;
        var preference = Read();
        if (!preference.AnnotationEnabled) result.AnnotationPosition = "hidden";
        else if (preference.AnnotationPosition != "skin") result.AnnotationPosition = preference.AnnotationPosition;
        if (skin.Manifest.AnnotationSupport == "none") result.AnnotationPosition = "hidden";
        if (preference.FontMode == "user") { result.FontPoint = preference.FontPoint; result.CommentFontPoint = preference.CommentFontPoint; }
        if (preference.Density != "skin") result.CandidateSpacing = preference.Density == "compact" ? 8 : 12;
        return result;
    }
}
