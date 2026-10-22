#include "../include/WeaselSkinAssets.h"
#include "SkinImageLoader.h"

#include <windows.h>
#include <aclapi.h>
#include <bcrypt.h>
#include <knownfolders.h>
#include <objbase.h>
#include <sddl.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string.h>
#include <memory>
#include <new>
#include <set>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace weasel {
namespace {

namespace fs = std::filesystem;

constexpr uint64_t kMaxSourceBytes = 100ull * 1024ull * 1024ull;
constexpr uint64_t kMaxDecodedBytes = 64ull * 1024ull * 1024ull;
constexpr size_t kMaxRecentBytes = 64ull * 1024ull;
constexpr int kRetentionDays = 30;

struct HandleCloser {
  void operator()(void* handle) const {
    if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
  }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

struct LocalCloser {
  void operator()(void* memory) const {
    if (memory) LocalFree(memory);
  }
};
using UniqueLocal = std::unique_ptr<void, LocalCloser>;

struct AlgorithmCloser {
  void operator()(void* handle) const {
    if (handle) BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(handle), 0);
  }
};

struct HashCloser {
  void operator()(void* handle) const {
    if (handle) BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(handle));
  }
};

std::string Win32Error(const std::string& stage, DWORD code) {
  return stage + " (win32=" + std::to_string(code) + ")";
}

std::string NtError(const std::string& stage, NTSTATUS status) {
  return stage + " (nt=" + std::to_string(static_cast<long>(status)) + ")";
}

std::wstring NormalizeForCompare(std::wstring text) {
  if (text.rfind(L"\\\\?\\UNC\\", 0) == 0) {
    text = L"\\\\" + text.substr(8);
  } else if (text.rfind(L"\\\\?\\", 0) == 0) {
    text = text.substr(4);
  }
  for (auto& ch : text) {
    if (ch == L'/') ch = L'\\';
  }
  while (text.size() > 3 && text.back() == L'\\') text.pop_back();
  return text;
}

bool PathEqual(const fs::path& left, const fs::path& right) {
  const std::wstring a = NormalizeForCompare(left.lexically_normal().wstring());
  const std::wstring b = NormalizeForCompare(right.lexically_normal().wstring());
  return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

std::wstring HexBytes(const BYTE* bytes, size_t count) {
  static constexpr wchar_t kHex[] = L"0123456789abcdef";
  std::wstring text;
  text.reserve(count * 2);
  for (size_t i = 0; i < count; ++i) {
    text.push_back(kHex[bytes[i] >> 4]);
    text.push_back(kHex[bytes[i] & 0x0f]);
  }
  return text;
}

std::wstring LowerExtension(const fs::path& path) {
  std::wstring extension = path.extension().wstring();
  for (auto& ch : extension) ch = static_cast<wchar_t>(std::towlower(ch));
  return extension;
}

bool IsAllowedExtension(const std::wstring& extension) {
  return extension == L".png" || extension == L".bmp" || extension == L".jpg" ||
         extension == L".jpeg" || extension == L".gif" || extension == L".tif" ||
         extension == L".tiff";
}

bool IsDigestName(const std::wstring& name, std::wstring* extension = nullptr) {
  const size_t dot = name.find_last_of(L'.');
  if (dot != 64) return false;
  const std::wstring suffix = name.substr(dot);
  if (!IsAllowedExtension(suffix)) return false;
  for (size_t i = 0; i < 64; ++i) {
    const wchar_t ch = name[i];
    if (!((ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'f'))) return false;
  }
  if (extension) *extension = suffix;
  return true;
}

bool ComputeSha256(const std::vector<BYTE>& data, std::array<BYTE, 32>& digest,
                   std::string& error) {
  BCRYPT_ALG_HANDLE raw_algorithm = nullptr;
  NTSTATUS status =
      BCryptOpenAlgorithmProvider(&raw_algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
  if (!BCRYPT_SUCCESS(status)) {
    error = NtError("hash open", status);
    return false;
  }
  std::unique_ptr<void, AlgorithmCloser> algorithm(raw_algorithm);
  DWORD hash_length = 0;
  DWORD written = 0;
  status = BCryptGetProperty(raw_algorithm, BCRYPT_HASH_LENGTH,
                             reinterpret_cast<PUCHAR>(&hash_length),
                             sizeof(hash_length), &written, 0);
  if (!BCRYPT_SUCCESS(status) || static_cast<size_t>(hash_length) != digest.size()) {
    error = NtError("hash length", status);
    return false;
  }
  BCRYPT_HASH_HANDLE raw_hash = nullptr;
  status = BCryptCreateHash(raw_algorithm, &raw_hash, nullptr, 0, nullptr, 0, 0);
  if (!BCRYPT_SUCCESS(status)) {
    error = NtError("hash create", status);
    return false;
  }
  std::unique_ptr<void, HashCloser> hash(raw_hash);
  if (!data.empty()) {
    status = BCryptHashData(raw_hash, const_cast<PUCHAR>(data.data()),
                            static_cast<ULONG>(data.size()), 0);
    if (!BCRYPT_SUCCESS(status)) {
      error = NtError("hash update", status);
      return false;
    }
  }
  status = BCryptFinishHash(raw_hash, digest.data(),
                            static_cast<ULONG>(digest.size()), 0);
  if (!BCRYPT_SUCCESS(status)) {
    error = NtError("hash finish", status);
    return false;
  }
  return true;
}

std::wstring FinalPath(HANDLE handle, std::string& error) {
  constexpr DWORD kFlags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
  DWORD size = GetFinalPathNameByHandleW(handle, nullptr, 0, kFlags);
  if (!size) {
    error = Win32Error("final path", GetLastError());
    return {};
  }
  std::wstring result(size, L'\0');
  DWORD count = GetFinalPathNameByHandleW(handle, &result[0], size, kFlags);
  if (!count || count >= size) {
    error = Win32Error("final path", count ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
    return {};
  }
  result.resize(count);
  return result;
}

bool SameFinalPath(const fs::path& input, const std::wstring& final_path) {
  std::error_code ec;
  const fs::path absolute = fs::absolute(input, ec);
  if (ec) return false;
  return _wcsicmp(NormalizeForCompare(absolute.lexically_normal().wstring()).c_str(),
                  NormalizeForCompare(final_path).c_str()) == 0;
}

bool ReadFileBytes(const fs::path& path, std::vector<BYTE>& data, size_t max_bytes,
                   std::string& error, const char* label, bool verify_final_path) {
  HANDLE raw = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
                               FILE_FLAG_SEQUENTIAL_SCAN,
                           nullptr);
  if (raw == INVALID_HANDLE_VALUE) {
    error = Win32Error(std::string(label) + " open", GetLastError());
    return false;
  }
  UniqueHandle file(raw);
  FILE_ATTRIBUTE_TAG_INFO attributes{};
  if (!GetFileInformationByHandleEx(raw, FileAttributeTagInfo, &attributes,
                                    sizeof(attributes))) {
    error = Win32Error(std::string(label) + " attributes", GetLastError());
    return false;
  }
  if (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
    error = std::string(label) + " reparse";
    return false;
  }
  if (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
    error = std::string(label) + " type";
    return false;
  }
  if (verify_final_path) {
    const std::wstring final_path = FinalPath(raw, error);
    if (final_path.empty()) return false;
    if (!SameFinalPath(path, final_path)) {
      error = std::string(label) + " final path";
      return false;
    }
  }
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(raw, &size)) {
    error = Win32Error(std::string(label) + " size", GetLastError());
    return false;
  }
  if (size.QuadPart < 0 ||
      static_cast<uint64_t>(size.QuadPart) > static_cast<uint64_t>(max_bytes)) {
    error = std::string(label) + " too large";
    return false;
  }
  data.resize(static_cast<size_t>(size.QuadPart));
  size_t offset = 0;
  while (offset < data.size()) {
    const DWORD chunk =
        static_cast<DWORD>(std::min<size_t>(data.size() - offset, 1u << 20));
    DWORD read = 0;
    if (!ReadFile(raw, data.data() + offset, chunk, &read, nullptr)) {
      error = Win32Error(std::string(label) + " read", GetLastError());
      return false;
    }
    if (!read) {
      error = std::string(label) + " short read";
      return false;
    }
    offset += read;
  }
  return true;
}

bool WriteFileBytes(HANDLE handle, const BYTE* data, size_t size,
                    std::string& error) {
  size_t offset = 0;
  while (offset < size) {
    const DWORD chunk = static_cast<DWORD>(std::min<size_t>(size - offset, 1u << 20));
    DWORD written = 0;
    if (!WriteFile(handle, data + offset, chunk, &written, nullptr)) {
      error = Win32Error("write", GetLastError());
      return false;
    }
    if (!written) {
      error = "write short";
      return false;
    }
    offset += written;
  }
  if (!FlushFileBuffers(handle)) {
    error = Win32Error("flush", GetLastError());
    return false;
  }
  return true;
}

bool ReadSmallFile(const fs::path& path, size_t max_bytes, std::string& content,
                   std::string& error) {
  std::vector<BYTE> data;
  if (!ReadFileBytes(path, data, max_bytes, error, "recent", false)) return false;
  if (data.empty()) {
    content.clear();
  } else {
    content.assign(reinterpret_cast<const char*>(data.data()), data.size());
  }
  return true;
}

bool GetCurrentUserSid(std::vector<BYTE>& sid, std::wstring& text,
                       std::string& error) {
  HANDLE raw_token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token)) {
    error = Win32Error("user token", GetLastError());
    return false;
  }
  UniqueHandle token(raw_token);
  DWORD length = 0;
  GetTokenInformation(raw_token, TokenUser, nullptr, 0, &length);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !length) {
    error = Win32Error("user token size", GetLastError());
    return false;
  }
  std::vector<BYTE> buffer(length);
  if (!GetTokenInformation(raw_token, TokenUser, buffer.data(), length, &length)) {
    error = Win32Error("user token info", GetLastError());
    return false;
  }
  TOKEN_USER* user = reinterpret_cast<TOKEN_USER*>(buffer.data());
  const DWORD sid_length = GetLengthSid(user->User.Sid);
  if (!sid_length) {
    error = "user sid";
    return false;
  }
  sid.assign(static_cast<BYTE*>(user->User.Sid),
             static_cast<BYTE*>(user->User.Sid) + sid_length);
  LPWSTR raw_text = nullptr;
  if (!ConvertSidToStringSidW(user->User.Sid, &raw_text)) {
    error = Win32Error("user sid text", GetLastError());
    return false;
  }
  UniqueLocal text_guard(raw_text);
  text.assign(raw_text);
  return true;
}

struct SecurityContext {
  std::wstring user_sid_string;
  UniqueLocal cache_dir_sd;
  UniqueLocal cache_file_sd;
  UniqueLocal state_dir_sd;
  UniqueLocal state_file_sd;
  UniqueLocal ancestor_sd;
  SECURITY_ATTRIBUTES cache_dir_sa{};
  SECURITY_ATTRIBUTES cache_file_sa{};
  SECURITY_ATTRIBUTES state_dir_sa{};
  SECURITY_ATTRIBUTES state_file_sa{};
  SECURITY_ATTRIBUTES ancestor_sa{};

  bool Build(std::string& error) {
    std::vector<BYTE> user_sid;
    if (!GetCurrentUserSid(user_sid, user_sid_string, error)) return false;
    const std::wstring common =
        L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;" + user_sid_string + L")";
    const std::wstring cache_dir =
        common + L"(A;OICI;FRFX;;;S-1-15-2-1)(A;OICI;FRFX;;;S-1-15-2-2)";
    const std::wstring cache_file =
        L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;" + user_sid_string +
        L")(A;;FR;;;S-1-15-2-1)(A;;FR;;;S-1-15-2-2)";
    const std::wstring state_file =
        L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;" + user_sid_string + L")";
    if (!Make(cache_dir, cache_dir_sd, error)) return false;
    if (!Make(cache_file, cache_file_sd, error)) return false;
    if (!Make(common, state_dir_sd, error)) return false;
    if (!Make(state_file, state_file_sd, error)) return false;
    if (!Make(common + L"(A;;0x20;;;S-1-15-2-1)(A;;0x20;;;S-1-15-2-2)", ancestor_sd, error)) return false;
    Initialize(cache_dir_sa, cache_dir_sd.get());
    Initialize(cache_file_sa, cache_file_sd.get());
    Initialize(state_dir_sa, state_dir_sd.get());
    Initialize(state_file_sa, state_file_sd.get());
    Initialize(ancestor_sa, ancestor_sd.get());
    return true;
  }

 private:
  static bool Make(const std::wstring& sddl, UniqueLocal& descriptor,
                   std::string& error) {
    PSECURITY_DESCRIPTOR raw = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, &raw, nullptr)) {
      error = Win32Error("acl build", GetLastError());
      return false;
    }
    descriptor.reset(raw);
    return true;
  }

  static void Initialize(SECURITY_ATTRIBUTES& attributes, void* descriptor) {
    attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    attributes.lpSecurityDescriptor = descriptor;
    attributes.bInheritHandle = FALSE;
  }
};

bool ApplyDacl(const fs::path& path, PSECURITY_DESCRIPTOR descriptor,
               const char* stage, std::string& error) {
  BOOL present = FALSE;
  PACL dacl = nullptr;
  BOOL defaulted = FALSE;
  if (!GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) ||
      !present || !dacl) {
    error = std::string(stage) + " dacl";
    return false;
  }
  const DWORD result = SetNamedSecurityInfoW(
      const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
      DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
      nullptr, dacl, nullptr);
  if (result != ERROR_SUCCESS) {
    error = Win32Error(stage, result);
    return false;
  }
  return true;
}

bool ApplyDaclToHandle(HANDLE handle, PSECURITY_DESCRIPTOR descriptor,
                       const char* stage, std::string& error) {
  BOOL present = FALSE;
  PACL dacl = nullptr;
  BOOL defaulted = FALSE;
  if (!GetSecurityDescriptorDacl(descriptor, &present, &dacl, &defaulted) ||
      !present || !dacl) {
    error = std::string(stage) + " dacl";
    return false;
  }
  const DWORD result = SetSecurityInfo(
      handle, SE_FILE_OBJECT,
      DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
      nullptr, dacl, nullptr);
  if (result != ERROR_SUCCESS) {
    error = Win32Error(stage, result);
    return false;
  }
  return true;
}

bool CheckAncestors(const fs::path& target, std::string& error) {
  for (fs::path check = target; !check.empty();) {
    const DWORD flags = GetFileAttributesW(check.c_str());
    if (flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_REPARSE_POINT)) {
      error = "ancestor reparse";
      return false;
    }
    if (flags == INVALID_FILE_ATTRIBUTES && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) {
      error = Win32Error("ancestor attributes", GetLastError());
      return false;
    }
    const auto parent = check.parent_path();
    if (parent == check) break;
    check = parent;
  }
  return true;
}

bool EnsureDirectory(const fs::path& target,
                     const SECURITY_ATTRIBUTES& target_attributes,
                     PSECURITY_DESCRIPTOR target_descriptor,
                     const SECURITY_ATTRIBUTES& ancestor_attributes,
                     PSECURITY_DESCRIPTOR ancestor_descriptor,
                     bool secure_existing, std::string& error) {
  // The leaf may exist behind a junction. Validate existing ancestors too,
  // without changing permissions on unrelated profile directories.
  if (!CheckAncestors(target, error)) return false;
  std::vector<fs::path> missing;
  fs::path current = target;
  for (;;) {
    const DWORD state = GetFileAttributesW(current.c_str());
    if (state != INVALID_FILE_ATTRIBUTES) {
      if (state & FILE_ATTRIBUTE_REPARSE_POINT) {
        error = "path reparse";
        return false;
      }
      if (!(state & FILE_ATTRIBUTE_DIRECTORY)) {
        error = "path type";
        return false;
      }
      break;
    }
    const DWORD code = GetLastError();
    if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) {
      error = Win32Error("path attributes", code);
      return false;
    }
    missing.push_back(current);
    const fs::path parent = current.parent_path();
    if (parent.empty() || parent == current) {
      error = "path root";
      return false;
    }
    current = parent;
  }
  for (auto it = missing.rbegin(); it != missing.rend(); ++it) {
    const bool is_target = PathEqual(*it, target);
    const SECURITY_ATTRIBUTES& attributes =
        is_target ? target_attributes : ancestor_attributes;
    PSECURITY_DESCRIPTOR descriptor =
        is_target ? target_descriptor : ancestor_descriptor;
    const bool created = CreateDirectoryW(it->c_str(), const_cast<SECURITY_ATTRIBUTES*>(&attributes)) != FALSE;
    if (!created) {
      const DWORD code = GetLastError();
      if (code == ERROR_ALREADY_EXISTS) {
        const DWORD state = GetFileAttributesW(it->c_str());
        if (state == INVALID_FILE_ATTRIBUTES ||
            (state & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !(state & FILE_ATTRIBUTE_DIRECTORY)) {
          error = "path race";
          return false;
        }
      } else {
        error = Win32Error("mkdir", code);
        return false;
      }
    }
    if ((created || is_target) && !ApplyDacl(*it, descriptor, "mkdir acl", error)) return false;
  }
  if (missing.empty() && secure_existing) {
    if (!ApplyDacl(target, target_descriptor, "path acl", error)) return false;
  }
  return true;
}

class StateLock {
 public:
  bool Acquire(const fs::path& state_root, const SecurityContext& security,
               std::string& error) {
    if (!EnsureDirectory(state_root, security.state_dir_sa,
                         security.state_dir_sd.get(), security.ancestor_sa,
                         security.ancestor_sd.get(), true, error)) {
      return false;
    }
    const fs::path lock_path = state_root / L"publisher.lock";
    const DWORD state = GetFileAttributesW(lock_path.c_str());
    if (state != INVALID_FILE_ATTRIBUTES &&
        (state & FILE_ATTRIBUTE_REPARSE_POINT)) {
      error = "lock reparse";
      return false;
    }
    HANDLE raw = CreateFileW(lock_path.c_str(),
                             GENERIC_READ | GENERIC_WRITE | WRITE_DAC, 0,
                             const_cast<SECURITY_ATTRIBUTES*>(
                                 &security.state_file_sa),
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (raw == INVALID_HANDLE_VALUE) {
      const DWORD code = GetLastError();
      if (code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION) {
        error = "busy";
        return false;
      }
      error = Win32Error("lock", code);
      return false;
    }
    if (!ApplyDaclToHandle(raw, security.state_file_sd.get(), "lock acl",
                           error)) {
      CloseHandle(raw);
      return false;
    }
    handle_.reset(raw);
    return true;
  }

 private:
  UniqueHandle handle_;
};

class StagingArea {
 public:
  StagingArea(const fs::path& directory, const SECURITY_ATTRIBUTES& attributes)
      : directory_(directory), attributes_(attributes) {}

  ~StagingArea() {
    for (const auto& path : created_) DeleteFileW(path.c_str());
  }

  bool Create(fs::path& path, UniqueHandle& handle, std::string& error) {
    for (int attempt = 0; attempt < 32; ++attempt) {
      BYTE random[16] = {};
      const NTSTATUS status = BCryptGenRandom(
          nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
      if (!BCRYPT_SUCCESS(status)) {
        error = NtError("staging random", status);
        return false;
      }
      const fs::path candidate =
          directory_ / (L"stage-" + HexBytes(random, sizeof(random)) + L".tmp");
      HANDLE raw = CreateFileW(candidate.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                               &attributes_, CREATE_NEW,
                               FILE_ATTRIBUTE_TEMPORARY, nullptr);
      if (raw != INVALID_HANDLE_VALUE) {
        created_.push_back(candidate);
        path = candidate;
        handle.reset(raw);
        return true;
      }
      const DWORD code = GetLastError();
      if (code != ERROR_FILE_EXISTS && code != ERROR_ALREADY_EXISTS) {
        error = Win32Error("staging create", code);
        return false;
      }
    }
    error = "staging name";
    return false;
  }

 private:
  fs::path directory_;
  SECURITY_ATTRIBUTES attributes_{};
  std::vector<fs::path> created_;
};

bool ValidateImage(const fs::path& path, uint64_t& decoded, std::string& error) {
  SkinImageLoadError load{};
  auto bitmap = LoadSkinBitmap(path.c_str(), load);
  if (!bitmap) {
    error = "image decode (stage=" + std::string(load.stage) +
            ", gdip=" + std::to_string(static_cast<int>(load.status)) +
            ", win32=" + std::to_string(load.file_error) + ")";
    return false;
  }
  decoded = static_cast<uint64_t>(bitmap->GetWidth()) * bitmap->GetHeight() * 4;
  return true;
}

bool CheckAcl(const fs::path& path, bool cache, bool directory) {
  std::vector<BYTE> user;
  std::wstring ignored_text;
  std::string ignored_error;
  if (!GetCurrentUserSid(user, ignored_text, ignored_error)) return false;
  PSID system = nullptr;
  PSID administrators = nullptr;
  PSID package_all = nullptr;
  PSID package_restricted = nullptr;
  if (!ConvertStringSidToSidW(L"S-1-5-18", &system)) return false;
  UniqueLocal system_guard(system);
  if (!ConvertStringSidToSidW(L"S-1-5-32-544", &administrators)) return false;
  UniqueLocal administrators_guard(administrators);
  if (!ConvertStringSidToSidW(L"S-1-15-2-1", &package_all)) return false;
  UniqueLocal package_all_guard(package_all);
  if (!ConvertStringSidToSidW(L"S-1-15-2-2", &package_restricted)) return false;
  UniqueLocal package_restricted_guard(package_restricted);
  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  const DWORD result = GetNamedSecurityInfoW(
      const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
      nullptr, nullptr, &dacl, nullptr, &descriptor);
  if (result != ERROR_SUCCESS) return false;
  UniqueLocal descriptor_guard(descriptor);
  SECURITY_DESCRIPTOR_CONTROL control = 0;
  DWORD revision = 0;
  if (!GetSecurityDescriptorControl(descriptor, &control, &revision)) return false;
  if (!(control & SE_DACL_PROTECTED)) return false;
  if (!(control & SE_DACL_PRESENT) || !dacl) return false;
  const DWORD write_mask =
      FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
      DELETE | WRITE_DAC | WRITE_OWNER | FILE_DELETE_CHILD | GENERIC_WRITE |
      GENERIC_ALL;
  const DWORD read_mask =
      directory ? (FILE_GENERIC_READ | FILE_TRAVERSE) : FILE_GENERIC_READ;
  bool user_full = false;
  bool system_full = false;
  bool administrators_full = false;
  bool package_all_read = false;
  bool package_restricted_read = false;
  GENERIC_MAPPING mapping = {FILE_GENERIC_READ, FILE_GENERIC_WRITE,
                             FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
  for (DWORD index = 0; index < dacl->AceCount; ++index) {
    LPVOID raw_ace = nullptr;
    if (!GetAce(dacl, index, &raw_ace)) return false;
    ACE_HEADER* header = static_cast<ACE_HEADER*>(raw_ace);
    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) return false;
    ACCESS_ALLOWED_ACE* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw_ace);
    PSID sid = &ace->SidStart;
    DWORD mask = ace->Mask;
    MapGenericMask(&mask, &mapping);
    const bool is_user = EqualSid(sid, user.data()) != FALSE;
    const bool is_system = EqualSid(sid, system) != FALSE;
    const bool is_administrators = EqualSid(sid, administrators) != FALSE;
    const bool is_package_all = EqualSid(sid, package_all) != FALSE;
    const bool is_package_restricted =
        EqualSid(sid, package_restricted) != FALSE;
    if (is_user && (mask & FILE_ALL_ACCESS) == FILE_ALL_ACCESS) user_full = true;
    if (is_system && (mask & FILE_ALL_ACCESS) == FILE_ALL_ACCESS) {
      system_full = true;
    }
    if (is_administrators && (mask & FILE_ALL_ACCESS) == FILE_ALL_ACCESS) {
      administrators_full = true;
    }
    if (is_package_all) {
      if (!cache) return false;
      if (mask & write_mask) return false;
      if ((mask & read_mask) == read_mask) package_all_read = true;
    }
    if (is_package_restricted) {
      if (!cache) return false;
      if (mask & write_mask) return false;
      if ((mask & read_mask) == read_mask) package_restricted_read = true;
    }
    if (!is_user && !is_system && !is_administrators && !is_package_all &&
        !is_package_restricted) {
      return false;
    }
  }
  if (!user_full || !system_full || !administrators_full) return false;
  if (cache) return package_all_read && package_restricted_read;
  return !package_all_read && !package_restricted_read;
}

bool TargetMatches(const fs::path& target,
                   const std::array<BYTE, 32>& expected, bool& matches,
                   std::string& error) {
  matches = false;
  const DWORD state = GetFileAttributesW(target.c_str());
  if (state == INVALID_FILE_ATTRIBUTES) {
    error = Win32Error("target attributes", GetLastError());
    return false;
  }
  if (state & FILE_ATTRIBUTE_REPARSE_POINT) {
    error = "target reparse";
    return false;
  }
  if (state & FILE_ATTRIBUTE_DIRECTORY) {
    error = "target type";
    return false;
  }
  std::vector<BYTE> data;
  if (!ReadFileBytes(target, data, static_cast<size_t>(kMaxSourceBytes), error,
                     "target", false)) {
    return false;
  }
  std::array<BYTE, 32> actual{};
  if (!ComputeSha256(data, actual, error)) return false;
  if (actual != expected) return true;
  matches = CheckAcl(target, true, false);
  return true;
}

bool ClearReadonly(const fs::path& path, std::string& error) {
  const DWORD state = GetFileAttributesW(path.c_str());
  if (state == INVALID_FILE_ATTRIBUTES) {
    error = Win32Error("attributes", GetLastError());
    return false;
  }
  if ((state & FILE_ATTRIBUTE_READONLY) &&
      !SetFileAttributesW(path.c_str(), state & ~FILE_ATTRIBUTE_READONLY)) {
    error = Win32Error("clear readonly", GetLastError());
    return false;
  }
  return true;
}

bool ApplyFileAcl(const fs::path& path, const SecurityContext& security,
                  bool cache, std::string& error) {
  void* descriptor =
      cache ? security.cache_file_sd.get() : security.state_file_sd.get();
  return ApplyDacl(path, descriptor, cache ? "file acl" : "state file acl",
                   error);
}

bool MoveIntoPlace(const fs::path& temporary, const fs::path& target,
                   std::string& error) {
  if (!MoveFileExW(temporary.c_str(), target.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    error = Win32Error("move", GetLastError());
    return false;
  }
  const DWORD state = GetFileAttributesW(target.c_str());
  if (state == INVALID_FILE_ATTRIBUTES) {
    error = Win32Error("move verify", GetLastError());
    return false;
  }
  if ((state & FILE_ATTRIBUTE_REPARSE_POINT) ||
      (state & FILE_ATTRIBUTE_DIRECTORY)) {
    error = "move verify";
    return false;
  }
  return true;
}

bool VerifyStaged(const fs::path& temporary,
                  const std::array<BYTE, 32>& expected, uint64_t& decoded,
                  std::string& error) {
  std::vector<BYTE> data;
  if (!ReadFileBytes(temporary, data, static_cast<size_t>(kMaxSourceBytes), error,
                     "staged", false)) {
    return false;
  }
  std::array<BYTE, 32> actual{};
  if (!ComputeSha256(data, actual, error)) return false;
  if (actual != expected) {
    error = "staged digest";
    return false;
  }
  return ValidateImage(temporary, decoded, error);
}

bool ParseRecent(const std::string& content,
                 std::vector<std::vector<std::wstring>>& records,
                 std::string& error) {
  if (content.size() > kMaxRecentBytes) {
    error = "recent size";
    return false;
  }
  std::vector<std::string> lines;
  size_t start = 0;
  for (;;) {
    const size_t newline = content.find('\n', start);
    std::string line =
        content.substr(start, newline == std::string::npos ? std::string::npos
                                                          : newline - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
    if (newline == std::string::npos) break;
    start = newline + 1;
  }
  if (!lines.empty() && lines.back().empty() && !content.empty() &&
      content.back() == '\n') {
    lines.pop_back();
  }
  if (lines.empty() || lines.front() != "version1") {
    error = "recent version";
    return false;
  }
  if (lines.size() > 3) {
    error = "recent lines";
    return false;
  }
  records.clear();
  for (size_t line_index = 1; line_index < lines.size(); ++line_index) {
    std::vector<std::wstring> set;
    if (!lines[line_index].empty()) {
      size_t token_start = 0;
      for (;;) {
        const size_t space = lines[line_index].find(' ', token_start);
        const std::string token = lines[line_index].substr(
            token_start, space == std::string::npos ? std::string::npos
                                                    : space - token_start);
        if (token.empty()) {
          error = "recent token";
          return false;
        }
        const std::wstring wide(token.begin(), token.end());
        if (!IsDigestName(wide)) {
          error = "recent token";
          return false;
        }
        set.push_back(wide);
        if (space == std::string::npos) break;
        token_start = space + 1;
      }
    }
    std::sort(set.begin(), set.end());
    set.erase(std::unique(set.begin(), set.end()), set.end());
    records.push_back(std::move(set));
  }
  return true;
}

bool SameSet(const std::vector<std::wstring>& left,
             const std::vector<std::wstring>& right) {
  if (left.size() != right.size()) return false;
  for (size_t i = 0; i < left.size(); ++i) {
    if (_wcsicmp(left[i].c_str(), right[i].c_str()) != 0) return false;
  }
  return true;
}

std::string Ascii(const std::wstring& text) {
  std::string result;
  result.reserve(text.size());
  for (const wchar_t ch : text) result.push_back(static_cast<char>(ch));
  return result;
}

bool WriteRecent(const fs::path& state_root,
                 const std::vector<std::vector<std::wstring>>& records,
                 const SecurityContext& security, StagingArea& staging,
                 std::string& error) {
  std::string content = "version1\n";
  for (const auto& set : records) {
    for (size_t i = 0; i < set.size(); ++i) {
      if (i) content.push_back(' ');
      content += Ascii(set[i]);
    }
    content.push_back('\n');
  }
  if (content.size() > kMaxRecentBytes) {
    error = "recent too large";
    return false;
  }
  fs::path temporary;
  UniqueHandle handle;
  if (!staging.Create(temporary, handle, error)) return false;
  if (!WriteFileBytes(handle.get(),
                      reinterpret_cast<const BYTE*>(content.data()),
                      content.size(), error)) {
    return false;
  }
  handle.reset();
  if (!ApplyFileAcl(temporary, security, false, error)) return false;
  const fs::path recent = state_root / L"recent.txt";
  const DWORD state = GetFileAttributesW(recent.c_str());
  if (state != INVALID_FILE_ATTRIBUTES) {
    if (state & FILE_ATTRIBUTE_REPARSE_POINT) {
      error = "recent reparse";
      return false;
    }
    if (state & FILE_ATTRIBUTE_DIRECTORY) {
      error = "recent type";
      return false;
    }
    if (!ClearReadonly(recent, error)) return false;
  }
  if (!MoveFileExW(temporary.c_str(), recent.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    error = Win32Error("recent move", GetLastError());
    return false;
  }
  return true;
}

void FilterLive(const std::vector<std::wstring>& live, const fs::path& root,
                std::vector<std::wstring>& accepted) {
  for (const auto& text : live) {
    std::error_code ec;
    fs::path path = fs::absolute(fs::path(text), ec);
    if (ec) continue;
    path = path.lexically_normal();
    if (!PathEqual(path.parent_path(), root)) continue;
    if (!IsDigestName(path.filename().wstring())) continue;
    const DWORD state = GetFileAttributesW(path.c_str());
    if (state == INVALID_FILE_ATTRIBUTES) continue;
    if (state & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) continue;
    accepted.push_back(path.filename().wstring());
  }
  std::sort(accepted.begin(), accepted.end(),
            [](const std::wstring& left, const std::wstring& right) {
              return _wcsicmp(left.c_str(), right.c_str()) < 0;
            });
  accepted.erase(std::unique(accepted.begin(), accepted.end(),
                             [](const std::wstring& left,
                                const std::wstring& right) {
                               return _wcsicmp(left.c_str(), right.c_str()) == 0;
                             }),
                 accepted.end());
}

bool Cleanup(const fs::path& root, const std::vector<std::wstring>& live,
             const std::vector<std::vector<std::wstring>>& records,
             std::string& error) {
  if (!CheckAncestors(root, error)) return false;
  const DWORD root_state = GetFileAttributesW(root.c_str());
  if (root_state == INVALID_FILE_ATTRIBUTES) {
    const DWORD code = GetLastError();
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) return true;
    error = Win32Error("cleanup root", code);
    return false;
  }
  if (root_state & FILE_ATTRIBUTE_REPARSE_POINT) {
    error = "cleanup root reparse";
    return false;
  }
  if (!(root_state & FILE_ATTRIBUTE_DIRECTORY)) {
    error = "cleanup root type";
    return false;
  }
  std::set<std::wstring> referenced;
  for (const auto& text : live) {
    referenced.insert(fs::path(text).filename().wstring());
  }
  for (const auto& set : records) {
    for (const auto& name : set) referenced.insert(name);
  }
  std::error_code ec;
  fs::directory_iterator iterator(root, fs::directory_options::skip_permission_denied,
                                  ec);
  if (ec) {
    error = Win32Error("cleanup list", static_cast<DWORD>(ec.value()));
    return false;
  }
  const fs::directory_iterator end;
  const auto cutoff = fs::file_time_type::clock::now() -
                      std::chrono::hours(24 * kRetentionDays);
  for (; iterator != end; iterator.increment(ec)) {
    if (ec) {
      error = Win32Error("cleanup list", static_cast<DWORD>(ec.value()));
      return false;
    }
    const fs::path path = iterator->path();
    const std::wstring name = path.filename().wstring();
    if (!IsDigestName(name)) continue;
    const DWORD state = GetFileAttributesW(path.c_str());
    if (state == INVALID_FILE_ATTRIBUTES) continue;
    if (state & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) continue;
    if (referenced.count(name)) continue;
    const auto modified = fs::last_write_time(path, ec);
    if (ec) {
      ec.clear();
      continue;
    }
    if (modified >= cutoff) continue;
    // References update access time explicitly, without invalidating renderer
    // caches keyed by modification time. Reads may extend retention as well.
    WIN32_FILE_ATTRIBUTE_DATA times = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &times)) continue;
    FILETIME now = {}; GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER last = {}, current = {};
    last.LowPart = times.ftLastAccessTime.dwLowDateTime; last.HighPart = times.ftLastAccessTime.dwHighDateTime;
    current.LowPart = now.dwLowDateTime; current.HighPart = now.dwHighDateTime;
    constexpr ULONGLONG retention = 30ULL * 24 * 60 * 60 * 10000000;
    if (last.QuadPart >= current.QuadPart || current.QuadPart - last.QuadPart < retention) continue;
    if (state & FILE_ATTRIBUTE_READONLY) {
      if (!SetFileAttributesW(path.c_str(), state & ~FILE_ATTRIBUTE_READONLY)) {
        continue;
      }
    }
    if (!DeleteFileW(path.c_str())) {
      const DWORD code = GetLastError();
      if (state & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(path.c_str(), state);
      if (code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION ||
          code == ERROR_ACCESS_DENIED) {
        continue;
      }
      continue;
    }
  }
  return true;
}

}  // namespace

SkinAssetPublisher::SkinAssetPublisher(std::filesystem::path root,
                                       std::filesystem::path stateRoot) {
  std::error_code ec;
  if (root.empty()) {
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,
                                       KF_FLAG_NO_PACKAGE_REDIRECTION, nullptr,
                                       &local)) &&
        local) {
      const fs::path base(local);
      CoTaskMemFree(local);
      root_ = (base / L"Rime" / L"SkinAssets" / L"v1").lexically_normal();
      if (stateRoot.empty()) {
        state_root_ =
            (base / L"Rime" / L"SkinAssetState" / L"v1").lexically_normal();
      }
    } else if (local) {
      CoTaskMemFree(local);
    }
  } else {
    fs::path absolute = fs::absolute(root, ec);
    root_ = (ec ? root : absolute).lexically_normal();
  }
  if (!stateRoot.empty()) {
    fs::path absolute = fs::absolute(stateRoot, ec);
    state_root_ = (ec ? stateRoot : absolute).lexically_normal();
  } else if (!root_.empty() && state_root_.empty()) {
    state_root_ = root_.parent_path() / L"private-state";
  }
}

bool SkinAssetPublisher::Publish(const std::vector<std::wstring>& sources,
                                 std::vector<std::wstring>& published,
                                 std::string& error) {
  error.clear();
  if (sources.empty()) {
    published.clear();
    return true;
  }
  try {
    if (root_.empty() || state_root_.empty()) {
      error = "root unavailable";
      return false;
    }
    SecurityContext security;
    if (!security.Build(error)) return false;
    StateLock lock;
    if (!lock.Acquire(state_root_, security, error)) return false;
    if (!EnsureDirectory(root_, security.cache_dir_sa,
                         security.cache_dir_sd.get(), security.ancestor_sa,
                         security.ancestor_sd.get(), true, error)) {
      return false;
    }
    const fs::path staging_root = state_root_ / L"staging";
    if (!EnsureDirectory(staging_root, security.state_dir_sa,
                         security.state_dir_sd.get(), security.ancestor_sa,
                         security.ancestor_sd.get(), true, error)) {
      return false;
    }
    StagingArea staging(staging_root, security.state_file_sa);
    std::vector<std::wstring> prepared;
    uint64_t total_bytes = 0;
    uint64_t total_decoded = 0;
    for (const auto& text : sources) {
      const fs::path source(text);
      const std::wstring extension = LowerExtension(source);
      if (!IsAllowedExtension(extension)) {
        error = "source extension";
        return false;
      }
      const uint64_t remaining = kMaxSourceBytes - total_bytes;
      std::vector<BYTE> bytes;
      if (!ReadFileBytes(source, bytes, static_cast<size_t>(remaining), error,
                         "source", true)) {
        return false;
      }
      ++hash_reads_;
      total_bytes += bytes.size();
      std::array<BYTE, 32> digest{};
      if (!ComputeSha256(bytes, digest, error)) return false;
      uint64_t decoded = 0;
      if (!ValidateImage(source, decoded, error)) return false;
      const std::wstring name = HexBytes(digest.data(), digest.size()) + extension;
      const fs::path target = root_ / fs::path(name);
      const DWORD state = GetFileAttributesW(target.c_str());
      if (state != INVALID_FILE_ATTRIBUTES) {
        if (state & FILE_ATTRIBUTE_REPARSE_POINT) {
          error = "target reparse";
          return false;
        }
        if (state & FILE_ATTRIBUTE_DIRECTORY) {
          error = "target type";
          return false;
        }
        bool matches = false;
        if (!TargetMatches(target, digest, matches, error)) return false;
        if (matches) {
          if (!ValidateImage(target, decoded, error)) return false;
          if (total_decoded + decoded > kMaxDecodedBytes) { error = "decoded total"; return false; }
          total_decoded += decoded;
          prepared.push_back(target.wstring());
          continue;
        }
        if (!ClearReadonly(target, error)) return false;
      }
      fs::path temporary;
      UniqueHandle handle;
      if (!staging.Create(temporary, handle, error)) return false;
      if (!WriteFileBytes(handle.get(), bytes.data(), bytes.size(), error)) {
        return false;
      }
      handle.reset();
      if (!VerifyStaged(temporary, digest, decoded, error)) return false;
      if (total_decoded + decoded > kMaxDecodedBytes) { error = "decoded total"; return false; }
      total_decoded += decoded;
      if (!ApplyFileAcl(temporary, security, true, error)) return false;
      if (!MoveIntoPlace(temporary, target, error)) return false;
      prepared.push_back(target.wstring());
    }
    published.swap(prepared);
    return true;
  } catch (const std::bad_alloc&) {
    error = "out of memory";
    return false;
  } catch (const fs::filesystem_error& exception) {
    error = "filesystem (win32=" + std::to_string(exception.code().value()) + ")";
    return false;
  } catch (...) {
    error = "unexpected";
    return false;
  }
}

bool SkinAssetPublisher::RecordAndCollect(const std::vector<std::wstring>& live,
                                          std::string& error) {
  error.clear();
  try {
    if (root_.empty() || state_root_.empty()) {
      error = "root unavailable";
      return false;
    }
    SecurityContext security;
    if (!security.Build(error)) return false;
    StateLock lock;
    if (!lock.Acquire(state_root_, security, error)) return false;
    const fs::path staging_root = state_root_ / L"staging";
    if (!EnsureDirectory(staging_root, security.state_dir_sa,
                         security.state_dir_sd.get(), security.ancestor_sa,
                         security.ancestor_sd.get(), true, error)) {
      return false;
    }
    StagingArea staging(staging_root, security.state_file_sa);
    std::vector<std::wstring> live_set;
    if (!CheckAncestors(root_, error)) return false;
    FilterLive(live, root_, live_set);
    for (const auto& name : live_set) {
      UniqueHandle file(CreateFileW((root_ / name).c_str(), FILE_WRITE_ATTRIBUTES,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
          OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
      FILETIME now = {}; GetSystemTimeAsFileTime(&now);
      if (file.get() == INVALID_HANDLE_VALUE || !SetFileTime(file.get(), nullptr, &now, nullptr)) {
        error = Win32Error("reference time", GetLastError()); return false;
      }
    }
    std::vector<std::vector<std::wstring>> records;
    const fs::path recent = state_root_ / L"recent.txt";
    const DWORD state = GetFileAttributesW(recent.c_str());
    if (state != INVALID_FILE_ATTRIBUTES) {
      if (state & FILE_ATTRIBUTE_REPARSE_POINT) {
        error = "recent reparse";
        return false;
      }
      if (state & FILE_ATTRIBUTE_DIRECTORY) {
        error = "recent type";
        return false;
      }
      std::string content;
      if (!ReadSmallFile(recent, kMaxRecentBytes, content, error)) return false;
      if (!ParseRecent(content, records, error)) return false;
    } else {
      const DWORD code = GetLastError();
      if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) {
        error = Win32Error("recent attributes", code);
        return false;
      }
    }
    const bool same = !records.empty() && SameSet(records.front(), live_set);
    if (!same) {
      records.insert(records.begin(), live_set);
      if (records.size() > 2) records.resize(2);
      if (!WriteRecent(state_root_, records, security, staging, error)) {
        return false;
      }
    }
    return Cleanup(root_, live_set, records, error);
  } catch (const std::bad_alloc&) {
    error = "out of memory";
    return false;
  } catch (const fs::filesystem_error& exception) {
    error = "filesystem (win32=" + std::to_string(exception.code().value()) + ")";
    return false;
  } catch (...) {
    error = "unexpected";
    return false;
  }
}

}  // namespace weasel
