#include "../include/WeaselSkinAssets.h"
#include "../WeaselUI/SkinImageLoader.h"

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <winioctl.h>
#include <gdiplus.h>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdiplus.lib")

#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
#define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL " << message << "\n";
    ++g_failures;
  }
}

bool Exists(const fs::path& path) {
  std::error_code ec;
  return fs::exists(path, ec) && !ec;
}

std::vector<BYTE> ReadAll(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary);
  std::vector<BYTE> data;
  char chunk[4096];
  while (stream) {
    stream.read(chunk, sizeof(chunk));
    const std::streamsize count = stream.gcount();
    if (count <= 0) break;
    const BYTE* begin = reinterpret_cast<const BYTE*>(chunk);
    data.insert(data.end(), begin, begin + static_cast<size_t>(count));
  }
  return data;
}

bool WriteAll(const fs::path& path, const std::vector<BYTE>& data) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) return false;
  if (!data.empty()) {
    stream.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
  }
  return stream.good();
}

bool WriteText(const fs::path& path, const std::string& text) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) return false;
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  return stream.good();
}

std::string Utf8(const std::wstring& text) {
  if (text.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                       static_cast<int>(text.size()), nullptr, 0,
                                       nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                      &result[0], size, nullptr, nullptr);
  return result;
}

fs::path MakeUniqueBase(const fs::path& test_root) {
  std::error_code ec;
  fs::create_directories(test_root, ec);
  if (ec) return {};
  const std::wstring prefix = L"skin-asset-publisher-test-" +
                              std::to_wstring(GetCurrentProcessId()) + L"-" +
                              std::to_wstring(GetTickCount64());
  for (int attempt = 0; attempt < 100; ++attempt) {
    const fs::path base =
        test_root /
        (prefix + (attempt ? L"-" + std::to_wstring(attempt) : std::wstring()));
    ec.clear();
    if (fs::exists(base, ec)) continue;
    ec.clear();
    if (fs::create_directories(base, ec) && !ec) return base;
    if (ec) return {};
  }
  return {};
}

fs::path StateOf(const fs::path& root) {
  return root.parent_path() / L"private-state";
}

bool MakeBmp(const fs::path& path, BYTE red, BYTE green, BYTE blue) {
  constexpr int kWidth = 2;
  constexpr int kHeight = 2;
  constexpr int kRowStride = ((kWidth * 3 + 3) / 4) * 4;
  constexpr int kPixelBytes = kRowStride * kHeight;
  BITMAPFILEHEADER file_header{};
  file_header.bfType = 0x4D42;
  file_header.bfOffBits =
      sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
  file_header.bfSize = file_header.bfOffBits + kPixelBytes;
  BITMAPINFOHEADER info{};
  info.biSize = sizeof(BITMAPINFOHEADER);
  info.biWidth = kWidth;
  info.biHeight = kHeight;
  info.biPlanes = 1;
  info.biBitCount = 24;
  info.biCompression = BI_RGB;
  info.biSizeImage = kPixelBytes;
  std::vector<BYTE> data(file_header.bfSize, 0);
  std::memcpy(data.data(), &file_header, sizeof(file_header));
  std::memcpy(data.data() + sizeof(file_header), &info, sizeof(info));
  BYTE* pixels = data.data() + file_header.bfOffBits;
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      BYTE* pixel = pixels + y * kRowStride + x * 3;
      pixel[0] = blue;
      pixel[1] = green;
      pixel[2] = red;
    }
  }
  return WriteAll(path, data);
}

struct MountPointBuffer { DWORD tag; USHORT length, reserved, substituteOffset, substituteLength, printOffset, printLength; wchar_t path[1]; };
bool MakeJunction(const fs::path& link, const fs::path& target) {
  if (!CreateDirectoryW(link.c_str(), nullptr)) return false;
  HANDLE handle =
      CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                  FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                  nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    RemoveDirectoryW(link.c_str());
    return false;
  }
  std::error_code ec;
  const fs::path absolute = fs::absolute(target, ec);
  if (ec) {
    CloseHandle(handle);
    RemoveDirectoryW(link.c_str());
    return false;
  }
  const std::wstring substitute = L"\\??\\" + absolute.wstring();
  const std::wstring print = absolute.wstring();
  const size_t path_bytes =
      (substitute.size() + print.size() + 2) * sizeof(wchar_t);
  const size_t total =
      offsetof(MountPointBuffer, path) +
      path_bytes;
  std::vector<BYTE> buffer(total, 0);
  auto* reparse = reinterpret_cast<MountPointBuffer*>(buffer.data());
  reparse->tag = IO_REPARSE_TAG_MOUNT_POINT;
  reparse->length = static_cast<USHORT>(total - 8);
  reparse->reserved = 0;
  reparse->substituteOffset = 0;
  reparse->substituteLength =
      static_cast<USHORT>(substitute.size() * sizeof(wchar_t));
  reparse->printOffset =
      static_cast<USHORT>((substitute.size() + 1) * sizeof(wchar_t));
  reparse->printLength =
      static_cast<USHORT>(print.size() * sizeof(wchar_t));
  wchar_t* path_buffer = reparse->path;
  std::memcpy(path_buffer, substitute.c_str(),
              (substitute.size() + 1) * sizeof(wchar_t));
  std::memcpy(path_buffer + substitute.size() + 1, print.c_str(),
              (print.size() + 1) * sizeof(wchar_t));
  DWORD returned = 0;
  const BOOL ok = DeviceIoControl(
      handle, FSCTL_SET_REPARSE_POINT, buffer.data(),
      static_cast<DWORD>(buffer.size()), nullptr, 0, &returned, nullptr);
  CloseHandle(handle);
  if (!ok) {
    RemoveDirectoryW(link.c_str());
    return false;
  }
  return true;
}

bool HasProtectedDacl(const fs::path& path) {
  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  const DWORD result = GetNamedSecurityInfoW(
      const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
      nullptr, nullptr, &dacl, nullptr, &descriptor);
  if (result != ERROR_SUCCESS) return false;
  SECURITY_DESCRIPTOR_CONTROL control = 0;
  DWORD revision = 0;
  const bool ok = GetSecurityDescriptorControl(descriptor, &control, &revision) &&
                  (control & SE_DACL_PROTECTED) &&
                  (control & SE_DACL_PRESENT) && dacl;
  LocalFree(descriptor);
  return ok;
}

bool CheckPackageAce(const fs::path& path, const wchar_t* sid_text,
                     bool expect_read) {
  PSID wanted = nullptr;
  if (!ConvertStringSidToSidW(sid_text, &wanted)) return false;
  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  const DWORD result = GetNamedSecurityInfoW(
      const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
      nullptr, nullptr, &dacl, nullptr, &descriptor);
  if (result != ERROR_SUCCESS) {
    LocalFree(wanted);
    return false;
  }
  const DWORD write_mask =
      FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
      DELETE | WRITE_DAC | WRITE_OWNER | FILE_DELETE_CHILD | GENERIC_WRITE |
      GENERIC_ALL;
  bool found = false;
  bool readable = false;
  bool writable = false;
  if (dacl) {
    for (DWORD index = 0; index < dacl->AceCount; ++index) {
      LPVOID raw_ace = nullptr;
      if (!GetAce(dacl, index, &raw_ace)) break;
      ACE_HEADER* header = static_cast<ACE_HEADER*>(raw_ace);
      if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) continue;
      ACCESS_ALLOWED_ACE* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw_ace);
      if (!EqualSid(&ace->SidStart, wanted)) continue;
      found = true;
      if ((ace->Mask & FILE_GENERIC_READ) == FILE_GENERIC_READ) readable = true;
      if (ace->Mask & write_mask) writable = true;
    }
  }
  LocalFree(descriptor);
  LocalFree(wanted);
  if (!expect_read) return !found;
  return found && readable && !writable;
}

bool FileIdentity(const fs::path& path, BY_HANDLE_FILE_INFORMATION& info) {
  HANDLE handle = CreateFileW(
      path.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  const BOOL ok = GetFileInformationByHandle(handle, &info);
  CloseHandle(handle);
  return ok != FALSE;
}

bool SameIdentity(const BY_HANDLE_FILE_INFORMATION& left,
                  const BY_HANDLE_FILE_INFORMATION& right) {
  return left.dwVolumeSerialNumber == right.dwVolumeSerialNumber &&
         left.nFileIndexHigh == right.nFileIndexHigh &&
         left.nFileIndexLow == right.nFileIndexLow &&
         CompareFileTime(&left.ftLastWriteTime, &right.ftLastWriteTime) == 0;
}

bool MakeOld(const fs::path& path, bool readonly_after) {
  DWORD state = GetFileAttributesW(path.c_str());
  if (state == INVALID_FILE_ATTRIBUTES) return false;
  if ((state & FILE_ATTRIBUTE_READONLY) &&
      !SetFileAttributesW(path.c_str(), state & ~FILE_ATTRIBUTE_READONLY)) {
    return false;
  }
  std::error_code ec;
  fs::last_write_time(
      path,
      fs::file_time_type::clock::now() - std::chrono::hours(24 * 31), ec);
  if (ec) return false;
  HANDLE handle = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return false;
  FILETIME now = {}; GetSystemTimeAsFileTime(&now);
  ULARGE_INTEGER time = {}; time.LowPart = now.dwLowDateTime; time.HighPart = now.dwHighDateTime;
  time.QuadPart -= 31ULL * 24 * 60 * 60 * 10000000;
  FILETIME old = {time.LowPart, time.HighPart};
  const bool aged = SetFileTime(handle, nullptr, &old, nullptr) != FALSE;
  CloseHandle(handle); if (!aged) return false;
  if (readonly_after) {
    state = GetFileAttributesW(path.c_str());
    if (state == INVALID_FILE_ATTRIBUTES) return false;
    if (!SetFileAttributesW(path.c_str(), state | FILE_ATTRIBUTE_READONLY)) {
      return false;
    }
  }
  return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 3) {
    std::cerr << "usage: skin_asset_publisher_test <sample.png> <writable-root>\n";
    return 2;
  }
  Gdiplus::GdiplusStartupInput input;
  ULONG_PTR token = 0;
  if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
    std::cerr << "FAIL GDI+ startup\n";
    return 2;
  }
  const fs::path sample = fs::absolute(argv[1]);
  const fs::path test_root = fs::absolute(argv[2]);
  if (!Exists(sample)) {
    std::cerr << "FAIL sample PNG missing\n";
    Gdiplus::GdiplusShutdown(token);
    return 2;
  }
  const fs::path base = MakeUniqueBase(test_root);
  if (base.empty()) {
    std::cerr << "FAIL cannot create isolated test root\n";
    Gdiplus::GdiplusShutdown(token);
    return 2;
  }
  std::cout << "TEST_ROOT " << Utf8(base.wstring()) << "\n";

  std::string error;
  const std::vector<BYTE> sample_bytes = ReadAll(sample);
  std::error_code ec;
  const fs::file_time_type sample_time = fs::last_write_time(sample, ec);
  Check(!ec, "read sample timestamp");

  {
    const fs::path no_root = base / L"no-root" / L"v1";
    weasel::SkinAssetPublisher publisher(no_root);
    std::vector<std::wstring> published{L"sentinel"};
    Check(publisher.Publish({}, published, error), "empty publish succeeds");
    Check(published.empty(), "empty publish clears output");
    Check(!Exists(no_root), "empty publish does not create root");
    Check(!Exists(StateOf(no_root)), "empty publish does not create state");
  }

  const fs::path main_root = base / L"main" / L"v1";
  weasel::SkinAssetPublisher publisher(main_root);
  Check(publisher.Root() == main_root, "Root returns explicit root");
  Check(publisher.HashReads() == 0, "initial hash reads zero");
  std::vector<std::wstring> first;
  Check(publisher.Publish({sample.wstring()}, first, error), "publish sample");
  Check(first.size() == 1, "one published path");
  Check(Exists(first.empty() ? fs::path() : fs::path(first[0])),
        "published target exists");
  if (first.size() == 1) {
    Check(ReadAll(fs::path(first[0])) == sample_bytes,
          "source and copy bytes equal");
    Check(ReadAll(sample) == sample_bytes, "source bytes unchanged");
    std::error_code timestamp_error;
    const fs::file_time_type after =
        fs::last_write_time(sample, timestamp_error);
    Check(!timestamp_error && after == sample_time, "source timestamp unchanged");
    Check(HasProtectedDacl(main_root), "cache root has protected DACL");
    Check(CheckPackageAce(main_root, L"S-1-15-2-1", true),
          "cache root grants package all read");
    Check(CheckPackageAce(main_root, L"S-1-15-2-2", true),
          "cache root grants package restricted read");
    Check(HasProtectedDacl(fs::path(first[0])), "target has protected DACL");
    Check(CheckPackageAce(fs::path(first[0]), L"S-1-15-2-1", true),
          "target grants package all read without write");
    Check(CheckPackageAce(fs::path(first[0]), L"S-1-15-2-2", true),
          "target grants package restricted read without write");
    Check(HasProtectedDacl(StateOf(main_root)), "state has protected DACL");
    Check(CheckPackageAce(StateOf(main_root), L"S-1-15-2-1", false),
          "state denies package all access");
    Check(CheckPackageAce(StateOf(main_root), L"S-1-15-2-2", false),
          "state denies package restricted access");
  }
  Check(publisher.HashReads() == 1, "hash reads count one source");

  if (first.size() == 1) {
    BY_HANDLE_FILE_INFORMATION before{};
    Check(FileIdentity(fs::path(first[0]), before), "target identity before");
    std::vector<std::wstring> again;
    Check(publisher.Publish({sample.wstring()}, again, error),
          "republish same sample");
    Check(again.size() == 1 && again[0] == first[0],
          "republish reuses same target path");
    BY_HANDLE_FILE_INFORMATION after{};
    Check(FileIdentity(fs::path(first[0]), after), "target identity after");
    Check(SameIdentity(before, after), "same target is not rewritten");
  }

  if (first.size() == 1) {
    Check(WriteText(fs::path(first[0]), "broken target"), "corrupt target");
    std::vector<std::wstring> repaired;
    Check(publisher.Publish({sample.wstring()}, repaired, error),
          "publish repairs invalid target");
    Check(repaired.size() == 1 && repaired[0] == first[0],
          "repair reuses same target path");
    Check(ReadAll(fs::path(first[0])) == sample_bytes,
          "repair restores verified bytes");
  }

  const fs::path red = base / L"red.bmp";
  const fs::path blue = base / L"blue.bmp";
  Check(MakeBmp(red, 255, 0, 0), "create red BMP");
  Check(MakeBmp(blue, 0, 0, 255), "create blue BMP");
  std::vector<std::wstring> red_out;
  std::vector<std::wstring> blue_out;
  Check(publisher.Publish({red.wstring()}, red_out, error), "publish red");
  Check(publisher.Publish({blue.wstring()}, blue_out, error), "publish blue");
  Check(red_out.size() == 1 && blue_out.size() == 1,
        "changed content returns one path each");
  if (red_out.size() == 1 && blue_out.size() == 1 && first.size() == 1) {
    Check(red_out[0] != first[0] && blue_out[0] != first[0] &&
              red_out[0] != blue_out[0],
          "changed content produces new digests");
    Check(ReadAll(fs::path(red_out[0])) == ReadAll(red),
          "red copy bytes equal");
    Check(ReadAll(fs::path(blue_out[0])) == ReadAll(blue),
          "blue copy bytes equal");
  }

  const fs::path fresh = base / L"fresh.bmp";
  Check(MakeBmp(fresh, 10, 20, 30), "create fresh BMP");
  std::vector<std::wstring> failed_out{L"sentinel"};
  std::string failed_error;
  Check(!publisher.Publish({fresh.wstring(), (base / L"missing.png").wstring()},
                           failed_out, failed_error),
        "missing source fails publish");
  Check(failed_out.size() == 1 && failed_out[0] == L"sentinel",
        "failure leaves output vector unchanged");
  Check(!failed_error.empty(), "failure reports error");
  Check(failed_error.find(Utf8(base.wstring())) == std::string::npos,
        "failure error omits private path");

  std::vector<std::wstring> rejected{L"sentinel"};
  Check(!publisher.Publish({(base / L"missing.png").wstring()}, rejected, error),
        "missing source rejected");
  Check(rejected.size() == 1 && rejected[0] == L"sentinel",
        "missing source leaves output unchanged");
  const fs::path corrupt = base / L"corrupt.png";
  Check(WriteText(corrupt, "not an image"), "write corrupt PNG");
  Check(!publisher.Publish({corrupt.wstring()}, rejected, error),
        "corrupt image rejected");
  const fs::path unknown = base / L"unknown.webp";
  Check(MakeBmp(unknown, 1, 2, 3), "write unknown extension");
  Check(!publisher.Publish({unknown.wstring()}, rejected, error),
        "unknown extension rejected");

  const fs::path link = base / L"link.png";
  if (CreateSymbolicLinkW(link.c_str(), sample.c_str(),
                          SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
    Check(!publisher.Publish({link.wstring()}, rejected, error),
          "file symlink rejected");
    DeleteFileW(link.c_str());
  } else {
    const fs::path junction = base / L"junction";
    if (MakeJunction(junction, sample.parent_path())) {
      const fs::path through = junction / sample.filename();
      Check(!publisher.Publish({through.wstring()}, rejected, error),
            "junction source rejected");
      RemoveDirectoryW(junction.c_str());
    } else {
      std::cout << "SKIP link rejection (cannot create symlink or junction)\n";
    }
  }

  {
    const fs::path lock_path = StateOf(main_root) / L"publisher.lock";
    HANDLE held = CreateFileW(lock_path.c_str(), GENERIC_READ, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(held != INVALID_HANDLE_VALUE, "open held lock");
    std::vector<std::wstring> busy_out;
    std::string busy_error;
    Check(!publisher.Publish({sample.wstring()}, busy_out, busy_error),
          "publish fails while lock is held");
    Check(busy_error.find("busy") != std::string::npos,
          "held lock reports busy");
    Check(busy_out.empty(), "busy publish leaves output unchanged");
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    std::vector<std::wstring> recovered;
    Check(publisher.Publish({sample.wstring()}, recovered, busy_error),
          "publish recovers after lock release");
    Check(recovered.size() == 1 && first.size() == 1 &&
              recovered[0] == first[0],
          "recovered publish reuses target");
  }

  const fs::path gc_root = base / L"gc" / L"v1";
  weasel::SkinAssetPublisher collector(gc_root);
  std::vector<std::wstring> a_out;
  std::vector<std::wstring> b_out;
  std::vector<std::wstring> c_out;
  Check(collector.Publish({sample.wstring()}, a_out, error), "gc publish A");
  Check(collector.Publish({red.wstring()}, b_out, error), "gc publish B");
  Check(collector.Publish({blue.wstring()}, c_out, error), "gc publish C");
  if (a_out.size() == 1 && b_out.size() == 1 && c_out.size() == 1) {
    Check(collector.RecordAndCollect({a_out[0]}, error), "record live A");
    Check(collector.RecordAndCollect({b_out[0]}, error), "record live B");
    Check(collector.RecordAndCollect({c_out[0]}, error), "record live C");
    Check(MakeOld(fs::path(a_out[0]), false), "age A");
    Check(MakeOld(fs::path(b_out[0]), true), "age B");
    Check(MakeOld(fs::path(c_out[0]), true), "age C");
    Check(collector.RecordAndCollect({c_out[0]}, error),
          "repeat same recent set");
    Check(!Exists(fs::path(a_out[0])), "expired unreferenced A deleted");
    Check(Exists(fs::path(b_out[0])), "second recent B protected");
    Check(Exists(fs::path(c_out[0])), "first recent C protected");
  }

  const fs::path bad_root = base / L"bad" / L"v1";
  weasel::SkinAssetPublisher bad(bad_root);
  std::vector<std::wstring> d_out;
  Check(bad.Publish({sample.wstring()}, d_out, error), "bad recent publish D");
  if (d_out.size() == 1) {
    const fs::path bad_state = StateOf(bad_root);
    Check(WriteText(bad_state / L"recent.txt", "version1\nnot-a-digest\n"),
          "write malformed recent");
    Check(MakeOld(fs::path(d_out[0]), false), "age D");
    std::string bad_error;
    Check(!bad.RecordAndCollect({}, bad_error), "malformed recent rejected");
    Check(Exists(fs::path(d_out[0])), "malformed recent stops cleanup");
    Check(!bad_error.empty(), "malformed recent reports error");
  }

  {
    weasel::SkinAssetPublisher retention(base / L"retention-cache", base / L"retention-state");
    std::vector<std::wstring> a, b, c;
    Check(retention.Publish({sample.wstring()}, a, error) && retention.Publish({red.wstring()}, b, error) &&
          retention.Publish({blue.wstring()}, c, error), "publish retention fixtures");
    if (a.size() == 1 && b.size() == 1 && c.size() == 1) {
      Check(MakeOld(a[0], false), "age previously published but active image");
      const auto stamp = fs::last_write_time(a[0]);
      Check(retention.RecordAndCollect(a, error) && retention.RecordAndCollect(b, error) &&
            retention.RecordAndCollect(c, error), "advance recently referenced image beyond two groups");
      Check(Exists(a[0]), "recently referenced old image retains its 30 day grace period");
      Check(fs::last_write_time(a[0]) == stamp, "reference tracking does not invalidate modification-time image cache");
    }
  }
  {
    const fs::path outside = base / L"outside", alias = base / L"cache-alias";
    fs::create_directories(outside / L"leaf");
    const fs::path victim = outside / L"leaf" / (std::wstring(64, L'a') + L".png");
    Check(WriteText(victim, "do not delete"), "write outside cleanup sentinel");
    Check(MakeOld(victim, false), "age outside cleanup sentinel");
    Check(MakeJunction(alias, outside), "create cleanup ancestor junction fixture");
    if (GetFileAttributesW(alias.c_str()) != INVALID_FILE_ATTRIBUTES) {
      weasel::SkinAssetPublisher unsafe(alias / L"leaf", base / L"gc-safe-state");
      Check(!unsafe.RecordAndCollect({}, error), "cleanup rejects ancestor junction");
      Check(Exists(victim), "cleanup cannot delete outside controlled root");
      RemoveDirectoryW(alias.c_str());
    }
  }

  std::vector<fs::path> generated;
  if (first.size() == 1) generated.push_back(fs::path(first[0]));
  if (red_out.size() == 1) generated.push_back(fs::path(red_out[0]));
  if (blue_out.size() == 1) generated.push_back(fs::path(blue_out[0]));
  if (b_out.size() == 1) generated.push_back(fs::path(b_out[0]));
  if (c_out.size() == 1) generated.push_back(fs::path(c_out[0]));
  if (d_out.size() == 1) generated.push_back(fs::path(d_out[0]));

  std::ofstream listing(base / L"published-paths.txt",
                        std::ios::binary | std::ios::trunc);
  if (listing) {
    listing << "TEST_ROOT " << Utf8(base.wstring()) << "\n";
    for (const auto& path : generated) {
      if (!Exists(path)) continue;
      std::cout << "PUBLISHED " << Utf8(path.wstring()) << "\n";
      listing << "PUBLISHED " << Utf8(path.wstring()) << "\n";
    }
    listing << "STATE " << Utf8(StateOf(main_root).wstring()) << "\n";
  }

  std::cout << (g_failures ? "FAIL" : "PASS")
            << " skin asset publisher tests, failures=" << g_failures << "\n";
  Gdiplus::GdiplusShutdown(token);
  return g_failures ? 1 : 0;
}
