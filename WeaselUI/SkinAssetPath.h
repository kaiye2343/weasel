#pragma once
#include <windows.h>
#include <memory>
#include <string>
#include <system_error>

namespace weasel {
// Absolute AppData strings can refer to different files in different process
// views. Export the location of the file actually opened by the skin loader.
// No package name or profile path is embedded in the skin or IPC protocol.
inline std::wstring ResolveSkinAssetPath(const std::wstring& path) {
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    throw std::system_error(GetLastError(), std::system_category(), "Cannot open skin asset");
  struct Close { void operator()(void* file) const { CloseHandle(file); } };
  std::unique_ptr<void, Close> file(handle);
  constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
  DWORD size = GetFinalPathNameByHandleW(handle, nullptr, 0, flags);
  if (!size) throw std::system_error(GetLastError(), std::system_category(), "Cannot resolve skin asset");
  std::wstring result(size, L'\0');
  DWORD count = GetFinalPathNameByHandleW(handle, &result[0], size, flags);
  if (!count || count >= size)
    throw std::system_error(count ? ERROR_INSUFFICIENT_BUFFER : GetLastError(),
        std::system_category(), "Cannot resolve skin asset");
  result.resize(count);
  // Keep the existing DOS/UNC style representation for GDI+ and old clients.
  if (result.compare(0, 8, L"\\\\?\\UNC\\") == 0) result = L"\\\\" + result.substr(8);
  else if (result.size() > 6 && result.compare(0, 4, L"\\\\?\\") == 0 && result[5] == L':')
    result.erase(0, 4);
  return result;
}
} // namespace weasel
