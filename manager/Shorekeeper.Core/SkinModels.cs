using System.Text.Json;
using System.Text.Json.Serialization;

namespace Shorekeeper.Core;

public sealed class SkinManifest
{
    [JsonRequired]
    public int SchemaVersion { get; set; } = 1;
    [JsonRequired]
    public string Id { get; set; } = "";
    [JsonRequired]
    public string Name { get; set; } = "";
    [JsonRequired]
    public string Author { get; set; } = "";
    [JsonRequired]
    public string Version { get; set; } = "";
    [JsonRequired]
    public string MinEngineVersion { get; set; } = "1.0";
    [JsonRequired]
    public string Preview { get; set; } = "";
    [JsonRequired]
    public List<SkinVariant> Variants { get; set; } = [];
}
public sealed class SkinVariant
{
    [JsonRequired]
    public string Id { get; set; } = "";
    [JsonRequired]
    public string Name { get; set; } = "";
    [JsonRequired]
    public SkinStyle Style { get; set; } = new();
    [JsonRequired]
    public List<SkinLayer> Layers { get; set; } = [];
}
public sealed class SkinStyle
{
    public AnnotationSeparator AnnotationSeparator { get; set; } = new();
    public string FontFace { get; set; } = "Microsoft YaHei UI";
    public int FontPoint { get; set; } = 16;
    public int CommentFontPoint { get; set; } = 11;
    public string AnnotationPosition { get; set; } = "beside";
    public int PaddingLeft { get; set; } = 16;
    public int PaddingRight { get; set; } = 16;
    public int PaddingTop { get; set; } = 12;
    public int PaddingBottom { get; set; } = 12;
    public int CandidateSpacing { get; set; } = 16;
    public int CornerRadius { get; set; } = 12;
    public int ShadowRadius { get; set; } = 6;
    public string ShadowColor { get; set; } = "#22101820";
    public string BackgroundColor { get; set; } = "#FFF5FBFF";
    public string TextColor { get; set; } = "#FF1C2F43";
    public string CommentColor { get; set; } = "#FF617F99";
    public string HighlightColor { get; set; } = "#FFDCEEFF";
    public string HighlightTextColor { get; set; } = "#FF207BC8";
    public string BorderColor { get; set; } = "#FF4DA9E9";
    public int BorderWidth { get; set; } = 2;
    public string BorderStyle { get; set; } = "solid";
    public int BorderInset { get; set; } = 0;
    public SkinFrame? Frame { get; set; }
}
public sealed class AnnotationSeparator
{
    public bool Enabled { get; set; } = true;
    public string Color { get; set; } = "#55000000";
    public int Thickness { get; set; } = 1;
    public bool FadeEnds { get; set; } = true;
}
public sealed class SkinFrame
{
    public string Path { get; set; } = "";
    public int[] Slice { get; set; } = [0, 0, 0, 0];
}
public sealed class SkinLayer
{
    public string Path { get; set; } = "";
    public string Anchor { get; set; } = "top_left";
    public int X { get; set; }
    public int Y { get; set; }
    public int Width { get; set; }
    public int Height { get; set; }
    public int Z { get; set; }
}
public sealed class SkinSelection
{
    public int SchemaVersion { get; set; } = 1;
    public string? SkinId { get; set; }
    public string? SkinVersion { get; set; }
    public string? VariantId { get; set; }
    public SkinStyle? StyleOverride { get; set; }
}
public sealed record InstalledSkin(string Directory, SkinManifest Manifest);

public static class SkinJson
{
    public static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        WriteIndented = true,
        RespectNullableAnnotations = true,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow
    };
    public static T Read<T>(string path) => JsonSerializer.Deserialize<T>(File.ReadAllText(path), Options)
        ?? throw new InvalidDataException("JSON内容为空。");
    public static void WriteAtomic<T>(string path, T value)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        string temp = path + "." + Guid.NewGuid().ToString("N") + ".tmp";
        try { File.WriteAllText(temp, JsonSerializer.Serialize(value, Options)); File.Move(temp, path, true); }
        finally { if (File.Exists(temp)) File.Delete(temp); }
    }
}
