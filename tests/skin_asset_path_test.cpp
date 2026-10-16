#include "../WeaselUI/SkinAssetPath.h"
#include "../WeaselUI/SkinImageLoader.h"
#include <filesystem>
#include <iostream>
#pragma comment(lib, "gdiplus.lib")

int wmain(int argc, wchar_t** argv) {
  if (argc != 3) return 2;
  const std::filesystem::path alias = std::filesystem::absolute(argv[1]);
  const std::filesystem::path original = std::filesystem::absolute(argv[2]);
  const auto attributes = GetFileAttributesW(alias.parent_path().c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
      !std::filesystem::equivalent(alias, original)) return 2;
  std::wstring resolved;
#ifdef SKIN_PATH_BASELINE
  resolved = alias.wstring(); // Original Asset() exported its logical absolute path.
#else
  resolved = weasel::ResolveSkinAssetPath(alias.wstring());
  if (_wcsicmp(resolved.c_str(), original.c_str()) != 0) return 3;
#endif
  // Remove only the fixture junction, never its target directory/files. A second
  // process view can no longer see the logical alias, but can read the real path.
  if (!RemoveDirectoryW(alias.parent_path().c_str())) return 2;
  Gdiplus::GdiplusStartupInput input;
  ULONG_PTR token = 0;
  if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return 2;
  bool loaded = false;
  {
    weasel::SkinImageLoadError error;
    loaded = !!weasel::LoadSkinBitmap(resolved.c_str(), error);
  }
  Gdiplus::GdiplusShutdown(token);
  if (!loaded) { std::cout << "FAIL logical asset path is unavailable in another view\n"; return 4; }
  bool missingRejected = false;
  try { weasel::ResolveSkinAssetPath(original.wstring() + L".missing"); }
  catch (const std::system_error&) { missingRejected = true; }
  if (!missingRejected) return 3;
  HANDLE exclusive = CreateFileW(original.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (exclusive == INVALID_HANDLE_VALUE) return 3;
  CloseHandle(exclusive);
  std::cout << "PASS physical asset path, cross-view decode, missing file rejection and handle release\n";
  return 0;
}
