using System.Text.Json;

namespace Shorekeeper.Core;

public sealed class DisplayPreferences
{
    public int SchemaVersion { get; set; } = 1;
    public string AnnotationPosition { get; set; } = "skin";
}
public sealed class DisplayPreferencesStore(string root)
{
    public string PathName => Path.Combine(root, "display.json");
    public DisplayPreferences Read()
    {
        var value = File.Exists(PathName) ? SkinJson.Read<DisplayPreferences>(PathName) : new();
        Validate(value); return value;
    }
    static void Validate(DisplayPreferences value)
    {
        if (value.SchemaVersion != 1 || value.AnnotationPosition is not ("skin" or "above" or "beside" or "hidden")) throw new InvalidDataException("候选注音偏好无效。");
    }
    public void Save(DisplayPreferences value) { Validate(value); SkinJson.WriteAtomic(PathName, value); }
    public SkinStyle EffectiveStyle(SkinStore store, InstalledSkin skin, SkinVariant variant)
    {
        var selected = store.Selection();
        var original = selected.SkinId == skin.Manifest.Id && selected.SkinVersion == skin.Manifest.Version && selected.VariantId == variant.Id && selected.StyleOverride != null ? selected.StyleOverride : variant.Style;
        var result = JsonSerializer.Deserialize<SkinStyle>(JsonSerializer.Serialize(original, SkinJson.Options), SkinJson.Options)!;
        var preference = Read(); if (preference.AnnotationPosition != "skin") result.AnnotationPosition = preference.AnnotationPosition;
        return result;
    }
}
