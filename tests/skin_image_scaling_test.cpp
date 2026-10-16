#include "../WeaselUI/SkinImageLoader.h"
#include <iostream>
#include <algorithm>
#pragma comment(lib, "gdiplus.lib")

static bool Compare(Gdiplus::Bitmap& source, int width, int height, UINT background) {
  auto cached = weasel::ScaleSkinBitmap(source, width, height);
  if (!cached || cached->GetWidth() != UINT(width) || cached->GetHeight() != UINT(height)) return false;
  Gdiplus::Bitmap direct(width + 16, height + 16, PixelFormat32bppPARGB);
  Gdiplus::Bitmap scaled(width + 16, height + 16, PixelFormat32bppPARGB);
  {
    Gdiplus::Graphics a(&direct), b(&scaled);
    a.Clear(Gdiplus::Color(background)); b.Clear(Gdiplus::Color(background));
    a.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    b.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
    if (a.DrawImage(&source, 8, 8, width, height) != Gdiplus::Ok ||
        b.DrawImage(cached.get(), 8, 8, width, height) != Gdiplus::Ok) return false;
  }
  Gdiplus::Rect rect(0, 0, width + 16, height + 16);
  Gdiplus::BitmapData a = {}, b = {};
  if (direct.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &a) != Gdiplus::Ok) return false;
  if (scaled.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &b) != Gdiplus::Ok) { direct.UnlockBits(&a); return false; }
  int largest = 0;
  for (int y = 0; y < rect.Height; ++y) for (int x = 0; x < rect.Width * 4; ++x) {
    int delta = int(*(static_cast<BYTE*>(a.Scan0) + ptrdiff_t(y) * a.Stride + x)) -
                int(*(static_cast<BYTE*>(b.Scan0) + ptrdiff_t(y) * b.Stride + x));
    largest = (std::max)(largest, delta < 0 ? -delta : delta);
  }
  scaled.UnlockBits(&b); direct.UnlockBits(&a);
  std::cout << "size=" << width << ',' << height << " background=" << background << " max_channel_delta=" << largest << '\n';
  return largest <= 2; // An intermediate 8-bit PARGB raster may round channels.
}
int wmain(int argc, wchar_t** argv) {
  if (argc != 2) return 2;
  Gdiplus::GdiplusStartupInput input; ULONG_PTR token = 0;
  if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return 2;
  bool passed = true;
  {
    weasel::SkinImageLoadError error; auto source = weasel::LoadSkinBitmap(argv[1], error);
    if (!source) return 2;
    for (int size : {96, 144, 192}) for (UINT color : {0u, 0xffffffffu, 0xff1c2130u})
      passed = Compare(*source, size, size + 32, color) && passed;
    passed = !weasel::ScaleSkinBitmap(*source, 0, 10) && !weasel::ScaleSkinBitmap(*source, 2049, 10) && passed;
  }
  Gdiplus::GdiplusShutdown(token);
  if (passed) std::cout << "PASS fitted-size pixels, alpha, light/dark backgrounds and dimension limits\n";
  return passed ? 0 : 3;
}
