using System.Runtime.InteropServices;

namespace Shorekeeper.Core;
// Windows' PNG decoder performs the final validation, matching the native renderer.
internal static class NativePng
{
    [StructLayout(LayoutKind.Sequential)]
    struct StartupInput
    {
        public uint Version;
        public nint Callback;
        [MarshalAs(UnmanagedType.Bool)] public bool SuppressThread;
        [MarshalAs(UnmanagedType.Bool)] public bool SuppressCodecs;
    }
    [DllImport("gdiplus.dll", ExactSpelling = true)] static extern int GdiplusStartup(out nuint token, ref StartupInput input, nint output);
    [DllImport("gdiplus.dll", ExactSpelling = true)] static extern void GdiplusShutdown(nuint token);
    [DllImport("gdiplus.dll", CharSet = CharSet.Unicode, ExactSpelling = true)] static extern int GdipLoadImageFromFile(string file, out nint image);
    [DllImport("gdiplus.dll", ExactSpelling = true)] static extern int GdipBitmapGetPixel(nint bitmap, int x, int y, out int argb);
    [DllImport("gdiplus.dll", ExactSpelling = true)] static extern int GdipDisposeImage(nint image);
    public static void Validate(string file, int width, int height)
    {
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException("原生皮肤工具仅支持Windows。");
        var input = new StartupInput { Version = 1 };
        if (GdiplusStartup(out var token, ref input, 0) != 0) throw new IOException("无法启动PNG解码器。");
        nint image = 0;
        try
        {
            if (GdipLoadImageFromFile(file, out image) != 0 || image == 0 || GdipBitmapGetPixel(image, 0, 0, out _) != 0 || GdipBitmapGetPixel(image, width - 1, height - 1, out _) != 0)
                throw new InvalidDataException("PNG无法完整解码。");
        }
        finally { if (image != 0) GdipDisposeImage(image); GdiplusShutdown(token); }
    }
}
