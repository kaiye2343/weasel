#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <memory>

namespace weasel {
// Preserve the failing API's status before GDI+ resets it in GetLastStatus().
// Do not log paths or input text: the caller identifies the manifest layer index.
struct SkinImageLoadError {
  const char* stage = "none";
  Gdiplus::Status status = Gdiplus::Ok;
  DWORD file_error = ERROR_SUCCESS;
  UINT width = 0;
  UINT height = 0;
};

inline std::unique_ptr<Gdiplus::Bitmap> LoadSkinBitmap(
    const wchar_t* path, SkinImageLoadError& error) {
  error = {};
  auto fail = [&](const char* stage, Gdiplus::Status status) {
    error.stage = stage;
    error.status = status;
    // A read-only probe distinguishes inaccessible files from codec/clone errors.
    HANDLE file = CreateFileW(path, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) error.file_error = GetLastError();
    else CloseHandle(file);
  };
  std::unique_ptr<Gdiplus::Bitmap> source(Gdiplus::Bitmap::FromFile(path));
  const auto loadStatus = source ? source->GetLastStatus() : Gdiplus::OutOfMemory;
  if (loadStatus != Gdiplus::Ok) {
    fail("load", loadStatus);
    return {};
  }
  error.width = source->GetWidth();
  error.height = source->GetHeight();
  if (!error.width || !error.height || error.width > 2048 || error.height > 2048) {
    fail("dimensions", Gdiplus::InvalidParameter);
    return {};
  }
  // The detached clone closes the source file, as in the existing renderer.
  std::unique_ptr<Gdiplus::Bitmap> bitmap(source->Clone(
      0, 0, static_cast<INT>(error.width), static_cast<INT>(error.height),
      PixelFormat32bppPARGB));
  const auto cloneStatus = bitmap ? bitmap->GetLastStatus() : source->GetLastStatus();
  if (!bitmap || cloneStatus != Gdiplus::Ok) {
    fail("clone", cloneStatus == Gdiplus::Ok ? Gdiplus::GenericError : cloneStatus);
    return {};
  }
  return bitmap;
}
} // namespace weasel
