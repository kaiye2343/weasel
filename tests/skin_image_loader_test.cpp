#include "../WeaselUI/SkinImageLoader.h"
#include <cassert>
#include <iostream>
#include <string>
#pragma comment(lib, "gdiplus.lib")

int wmain(int argc, wchar_t** argv) {
  assert(argc == 2);
  Gdiplus::GdiplusStartupInput input;
  ULONG_PTR token = 0;
  assert(Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok);
  {
    weasel::SkinImageLoadError error;
    auto bitmap = weasel::LoadSkinBitmap(argv[1], error);
    assert(bitmap && error.status == Gdiplus::Ok);
    const UINT width = bitmap->GetWidth(), height = bitmap->GetHeight();
    assert(width && height && bitmap->GetPixelFormat() == PixelFormat32bppPARGB);
    {
      Gdiplus::Bitmap original(argv[1]);
      assert(original.GetLastStatus() == Gdiplus::Ok);
      Gdiplus::Rect rect(0, 0, width, height);
      Gdiplus::BitmapData from = {}, to = {};
      assert(original.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &from) == Gdiplus::Ok);
      assert(bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &to) == Gdiplus::Ok);
      for (UINT y = 0; y < height; ++y)
        assert(std::memcmp(static_cast<const BYTE*>(from.Scan0) + static_cast<ptrdiff_t>(y) * from.Stride,
                           static_cast<const BYTE*>(to.Scan0) + static_cast<ptrdiff_t>(y) * to.Stride,
                           static_cast<size_t>(width) * 4) == 0);
      bitmap->UnlockBits(&to);
      original.UnlockBits(&from);
    }
    // A successfully cached bitmap must not keep the PNG open.
    HANDLE file = CreateFileW(argv[1], GENERIC_READ, 0, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(file != INVALID_HANDLE_VALUE);
    // Exercise the real decoder seam under an exclusive file lock, without UI.
    auto failed = weasel::LoadSkinBitmap(argv[1], error);
    std::cout << "locked stage=" << error.stage << " gdip=" << error.status
              << " file_error=" << error.file_error << '\n';
    assert(!failed && std::string(error.stage) == "load");
    assert(error.status != Gdiplus::Ok && error.file_error == ERROR_SHARING_VIOLATION);
    CloseHandle(file);
    auto recovered = weasel::LoadSkinBitmap(argv[1], error);
    assert(recovered && error.status == Gdiplus::Ok && error.file_error == ERROR_SUCCESS);
    assert(recovered->GetWidth() == width && recovered->GetHeight() == height);
    Gdiplus::Color pixel;
    assert(bitmap->GetPixel(0, 0, &pixel) == Gdiplus::Ok);
    auto missing = std::wstring(argv[1]) + L".missing";
    failed = weasel::LoadSkinBitmap(missing.c_str(), error);
    std::cout << "missing stage=" << error.stage << " gdip=" << error.status
              << " file_error=" << error.file_error << '\n';
    assert(!failed && std::string(error.stage) == "load");
    assert(error.status != Gdiplus::Ok && error.file_error == ERROR_FILE_NOT_FOUND);
  }
  Gdiplus::GdiplusShutdown(token);
  std::cout << "PASS decode, identical alpha/pixels, detached cache, locked file, recovery and missing-file diagnostics\n";
}
