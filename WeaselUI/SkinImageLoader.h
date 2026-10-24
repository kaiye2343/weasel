#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <memory>
#include <cstring>
#include <cstddef>
#include "../include/WeaselPerformance.h"

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
  weasel::performance::processProbe.Add(
      weasel::performance::Counter::Decode);
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
  // Clone retains GDI+'s source-file dependency. Copy decoded pixels into a fresh
  // allocation instead, so the cached image has no file/decoder lifetime link.
  auto bitmap = std::make_unique<Gdiplus::Bitmap>(
      static_cast<INT>(error.width), static_cast<INT>(error.height), PixelFormat32bppPARGB);
  auto status = bitmap->GetLastStatus();
  if (status != Gdiplus::Ok) {
    fail("allocate", status);
    return {};
  }
  Gdiplus::Rect rect(0, 0, static_cast<INT>(error.width), static_cast<INT>(error.height));
  Gdiplus::BitmapData from = {}, to = {};
  status = source->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &from);
  if (status != Gdiplus::Ok) { fail("decode_pixels", status); return {}; }
  status = bitmap->LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB, &to);
  if (status != Gdiplus::Ok) {
    source->UnlockBits(&from);
    fail("copy_pixels", status);
    return {};
  }
  for (UINT y = 0; y < error.height; ++y)
    std::memcpy(static_cast<BYTE*>(to.Scan0) + static_cast<ptrdiff_t>(y) * to.Stride,
                static_cast<const BYTE*>(from.Scan0) + static_cast<ptrdiff_t>(y) * from.Stride,
                static_cast<size_t>(error.width) * 4);
  const auto destinationStatus = bitmap->UnlockBits(&to);
  const auto sourceStatus = source->UnlockBits(&from);
  if (destinationStatus != Gdiplus::Ok || sourceStatus != Gdiplus::Ok) {
    fail("unlock_pixels", destinationStatus != Gdiplus::Ok ? destinationStatus : sourceStatus);
    return {};
  }
  return bitmap;
}
// Rasterize decorations once per fitted size/DPI, then composite them 1:1.
inline std::unique_ptr<Gdiplus::Bitmap> ScaleSkinBitmap(
    Gdiplus::Bitmap& source, int width, int height,
    Gdiplus::PixelOffsetMode offset = Gdiplus::PixelOffsetModeDefault) {
  if (width < 1 || height < 1 || width > 2048 || height > 2048) return {};
  weasel::performance::Scope scaleProbe(
      weasel::performance::processProbe,
      weasel::performance::Stage::ResourcePrepare);
  weasel::performance::processProbe.Add(
      weasel::performance::Counter::Scale);
  std::unique_ptr<Gdiplus::Bitmap> bitmap;
  try { bitmap = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppPARGB); }
  catch (const std::bad_alloc&) { return {}; }
  if (bitmap->GetLastStatus() != Gdiplus::Ok) return {};
  Gdiplus::Graphics graphics(bitmap.get());
  graphics.Clear(Gdiplus::Color(0));
  graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
  graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  graphics.SetPixelOffsetMode(offset);
  if (graphics.DrawImage(&source, 0, 0, width, height) != Gdiplus::Ok) return {};
  graphics.Flush(Gdiplus::FlushIntentionSync);
  return bitmap;
}
} // namespace weasel
