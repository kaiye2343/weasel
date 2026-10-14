using System.Buffers.Binary;
using System.IO.Compression;
using System.Security.Cryptography;
using System.Text.RegularExpressions;

namespace Shorekeeper.Core;

public sealed class SkinStore
{
    public const long MaxArchiveBytes = 50L * 1024 * 1024;
    public const long MaxExpandedBytes = 100L * 1024 * 1024;
    public const long MaxDecodedBytes = 64L * 1024 * 1024;
    public string Root { get; }
    public string StatePath => Path.Combine(Root, "state.json");
    public static string DefaultRoot => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ShorekeeperWeasel");
    public SkinStore(string? root = null) => Root = Path.GetFullPath(root ?? DefaultRoot);
    public static bool IsId(string value) => Regex.IsMatch(value, "^[a-z0-9][a-z0-9._-]{0,63}$") && !value.Contains("..");
    public static bool IsVersion(string value) => Regex.IsMatch(value, "^[0-9]+\\.[0-9]+\\.[0-9]+$");

    public static string ResolveAsset(string root, string relative)
    {
        if (string.IsNullOrWhiteSpace(relative) || relative.Length > 240 || relative.Contains('\\') || relative.Contains(':') ||
            relative.StartsWith('/') || relative.Split('/').Any(p => p is "" or "." or ".." || p.EndsWith(' ') || p.EndsWith('.') ||
                Regex.IsMatch(p, "^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])($|\\.)", RegexOptions.IgnoreCase)))
            throw new InvalidDataException("皮肤包含不安全的路径。");
        string full = Path.GetFullPath(Path.Combine(root, relative));
        if (!full.StartsWith(Path.GetFullPath(root) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("皮肤路径越界。");
        return full;
    }
    public static (int Width, int Height) InspectPng(string path)
    {
        using var stream = File.OpenRead(path);
        Span<byte> header = stackalloc byte[24];
        stream.ReadExactly(header);
        byte[] signature = [137, 80, 78, 71, 13, 10, 26, 10];
        if (!header[..8].SequenceEqual(signature) || !header[12..16].SequenceEqual("IHDR"u8))
            throw new InvalidDataException("图片不是PNG。");
        int width = checked((int)BinaryPrimitives.ReadUInt32BigEndian(header[16..20]));
        int height = checked((int)BinaryPrimitives.ReadUInt32BigEndian(header[20..24]));
        if (width is < 1 or > 2048 || height is < 1 or > 2048) throw new InvalidDataException("PNG尺寸超出限制。");
        stream.Position = 8;
        bool ended = false, hasData = false;
        byte[] chunkHeader = new byte[8];
        while (stream.Position < stream.Length)
        {
            stream.ReadExactly(chunkHeader);
            int size = checked((int)BinaryPrimitives.ReadUInt32BigEndian(chunkHeader));
            if (size < 0 || size > MaxExpandedBytes || stream.Length - stream.Position < size + 4L)
                throw new InvalidDataException("PNG数据不完整。");
            byte[] data = new byte[size + 4];
            chunkHeader.AsSpan(4).CopyTo(data);
            stream.ReadExactly(data.AsSpan(4));
            byte[] crcBytes = new byte[4]; stream.ReadExactly(crcBytes);
            uint crc = 0xffffffff;
            foreach (byte b in data) { crc ^= b; for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ ((crc & 1) != 0 ? 0xedb88320u : 0); }
            if (~crc != BinaryPrimitives.ReadUInt32BigEndian(crcBytes)) throw new InvalidDataException("PNG校验失败。");
            if (data.AsSpan(0, 4).SequenceEqual("IDAT"u8)) hasData = true;
            if (data.AsSpan(0, 4).SequenceEqual("IEND"u8)) { ended = true; break; }
        }
        if (!ended || !hasData || stream.Position != stream.Length) throw new InvalidDataException("PNG数据不完整。");
        return (width, height);
    }
    public static SkinManifest ValidateDirectory(string root)
    {
        string manifestPath = Path.Combine(root, "manifest.json");
        if (new FileInfo(manifestPath).Length > 1024 * 1024) throw new InvalidDataException("清单过大。");
        var m = SkinJson.Read<SkinManifest>(manifestPath);
        if (m.SchemaVersion != 1 || !IsId(m.Id) || !IsVersion(m.Version) || string.IsNullOrWhiteSpace(m.Name) ||
            string.IsNullOrWhiteSpace(m.Author) || m.Variants.Count is < 1 or > 32 ||
            m.MinEngineVersion != "1.0")
            throw new InvalidDataException("皮肤标识、版本或兼容版本无效。");
        InspectPng(ResolveAsset(root, m.Preview));
        var ids = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var v in m.Variants)
        {
            if (!IsId(v.Id) || !ids.Add(v.Id) || v.Layers.Count > 8) throw new InvalidDataException("变体标识或层数无效。");
            ValidateStyle(v.Style);
            long decoded = 0;
            foreach (var layer in v.Layers)
            {
                if (layer.Anchor is not ("top_left" or "top_right" or "bottom_left" or "bottom_right") ||
                    layer.Width is < 1 or > 512 || layer.Height is < 1 or > 512 ||
                    layer.X is < -512 or > 512 || layer.Y is < -512 or > 512 || layer.Z is < -100 or > 100)
                    throw new InvalidDataException("图片布局超出限制。");
                var size = InspectPng(ResolveAsset(root, layer.Path)); decoded += (long)size.Width * size.Height * 4;
            }
            if (v.Style.Frame is { } frame)
            {
                var size = InspectPng(ResolveAsset(root, frame.Path)); decoded += (long)size.Width * size.Height * 4;
                if (frame.Slice.Length != 4 || frame.Slice.Any(x => x is < 0 or > 2048) || frame.Slice[0] + frame.Slice[2] >= size.Width || frame.Slice[1] + frame.Slice[3] >= size.Height)
                    throw new InvalidDataException("九宫格切片无效。");
            }
            if (decoded > MaxDecodedBytes) throw new InvalidDataException("活动皮肤解码内存超过64MiB。");
        }
        return m;
    }
    public static void ValidateStyle(SkinStyle style)
    {
        if (style.AnnotationPosition is not ("above" or "beside" or "hidden") || style.BorderStyle is not ("none" or "solid" or "dash") ||
            style.FontPoint is < 8 or > 48 || style.CommentFontPoint is < 8 or > 32 || style.BorderWidth is < 0 or > 12 ||
            style.BorderInset is < 0 or > 32 || style.CornerRadius is < 0 or > 64 || style.ShadowRadius is < 0 or > 32 ||
            new[] { style.PaddingLeft, style.PaddingRight, style.PaddingTop, style.PaddingBottom, style.CandidateSpacing }.Any(n => n is < 0 or > 256) ||
            style.FontFace.Length is < 1 or > 128)
            throw new InvalidDataException("样式参数无效。");
        foreach (string color in new[] { style.BackgroundColor, style.TextColor, style.CommentColor, style.HighlightColor, style.HighlightTextColor, style.BorderColor, style.ShadowColor })
            if (!Regex.IsMatch(color, "^#[0-9A-Fa-f]{8}$")) throw new InvalidDataException("颜色须为#AARRGGBB。");
    }
    public InstalledSkin Import(string archivePath)
    {
        if (new FileInfo(archivePath).Length > MaxArchiveBytes) throw new InvalidDataException("皮肤包超过50MiB。");
        Directory.CreateDirectory(Root);
        string staging = Path.Combine(Root, ".staging-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(staging);
        try
        {
            using (var archive = ZipFile.OpenRead(archivePath))
            {
                long total = 0; var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                if (archive.Entries.Count > 256) throw new InvalidDataException("皮肤包文件过多。");
                foreach (var entry in archive.Entries)
                {
                    if (entry.FullName.EndsWith('/')) { ResolveAsset(staging, entry.FullName.TrimEnd('/')); continue; }
                    string target = ResolveAsset(staging, entry.FullName);
                    if (!names.Add(entry.FullName) || ((entry.ExternalAttributes >> 16) & 0xf000) == 0xa000 ||
                        (Path.GetExtension(target).ToLowerInvariant() is not (".png" or ".json")))
                        throw new InvalidDataException("包含重复路径、链接或不支持的文件。");
                    total = checked(total + entry.Length);
                    if (total > MaxExpandedBytes) throw new InvalidDataException("解压内容超过100MiB。");
                    Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                    using var input = entry.Open(); using var output = File.Create(target);
                    byte[] buffer = new byte[65536]; long written = 0; int count;
                    while ((count = input.Read(buffer)) > 0)
                    {
                        written += count;
                        if (written > entry.Length) throw new InvalidDataException("压缩内容长度异常。");
                        output.Write(buffer, 0, count);
                    }
                    if (written != entry.Length) throw new InvalidDataException("压缩内容不完整。");
                }
            }
            var manifest = ValidateDirectory(staging);
            string destination = Path.Combine(Root, "skins", manifest.Id, manifest.Version);
            if (Directory.Exists(destination)) throw new InvalidDataException("该版本已存在；请使用新版本号，已有版本保持不变。");
            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            Directory.Move(staging, destination);
            return new(destination, manifest);
        }
        finally
        {
            if (staging.StartsWith(Root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase) && Directory.Exists(staging)) Directory.Delete(staging, true);
        }
    }
    public IReadOnlyList<InstalledSkin> List()
    {
        string skins = Path.Combine(Root, "skins");
        if (!Directory.Exists(skins)) return [];
        var result = new List<InstalledSkin>();
        foreach (string id in Directory.EnumerateDirectories(skins))
            foreach (string version in Directory.EnumerateDirectories(id))
                try { result.Add(new(version, ValidateDirectory(version))); }
                catch (Exception e) when (e is IOException or InvalidDataException or System.Text.Json.JsonException or OverflowException) { }
        return result;
    }
    public SkinSelection Selection() => File.Exists(StatePath) ? SkinJson.Read<SkinSelection>(StatePath) : new();
    public void Select(InstalledSkin skin, string variant, SkinStyle? style = null)
    {
        var validated = ValidateDirectory(skin.Directory);
        if (!validated.Variants.Any(v => v.Id == variant)) throw new InvalidDataException("变体不存在。");
        if (style != null)
        {
            ValidateStyle(style);
            var candidate = validated.Variants.First(v => v.Id == variant);
            candidate.Style = style;
            // Validate overrides with the same image budget and slice rules as import.
            ValidateResources(skin.Directory, candidate);
        }
        SkinJson.WriteAtomic(StatePath, new SkinSelection { SkinId = validated.Id, SkinVersion = validated.Version, VariantId = variant, StyleOverride = style });
    }
    public void Reset() => SkinJson.WriteAtomic(StatePath, new SkinSelection());
    static void ValidateResources(string root, SkinVariant variant)
    {
        long bytes = 0;
        foreach (var layer in variant.Layers) { var size = InspectPng(ResolveAsset(root, layer.Path)); bytes += (long)size.Width * size.Height * 4; }
        if (variant.Style.Frame is { } frame)
        {
            var size = InspectPng(ResolveAsset(root, frame.Path)); bytes += (long)size.Width * size.Height * 4;
            if (frame.Slice.Length != 4 || frame.Slice.Any(x => x is < 0 or > 2048) || frame.Slice[0] + frame.Slice[2] >= size.Width || frame.Slice[1] + frame.Slice[3] >= size.Height) throw new InvalidDataException("九宫格切片无效。");
        }
        if (bytes > MaxDecodedBytes) throw new InvalidDataException("活动皮肤解码内存超过64MiB。");
    }
    public void Export(InstalledSkin skin, string destination)
    {
        ValidateDirectory(skin.Directory);
        if (File.Exists(destination)) throw new IOException("导出目标已存在。");
        ZipFile.CreateFromDirectory(skin.Directory, destination);
    }
}
