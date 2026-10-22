// No windows or input hooks: exercise the production decoder under real tokens.
#include <windows.h>
#include <userenv.h>
#include <sddl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "../include/WeaselSkinAssets.h"
#include "../WeaselUI/SkinImageLoader.h"
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace fs = std::filesystem;
static void Require(bool value, const char* stage) {
  if (!value) throw std::runtime_error(std::string(stage) + " win32=" + std::to_string(GetLastError()));
}
static void Access(const fs::path& path, bool published) {
  HANDLE token = nullptr;
  Require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != FALSE, "token");
  DWORD size = 0; GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  std::vector<BYTE> bytes(size);
  Require(GetTokenInformation(token, TokenUser, bytes.data(), size, &size) != FALSE, "user"); CloseHandle(token);
  LPWSTR sid = nullptr;
  Require(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid) != FALSE, "sid");
  std::wstring sddl = L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;" + std::wstring(sid) + L")";
  LocalFree(sid);
  if (published) sddl += L"(A;OICI;GRGX;;;S-1-15-2-1)(A;OICI;GRGX;;;S-1-15-2-2)";
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  Require(ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr) != FALSE, "sddl");
  bool ok = SetFileSecurityW(path.c_str(), DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor) != FALSE;
  LocalFree(descriptor); Require(ok, "acl");
}
static unsigned long long Pixels(Gdiplus::Bitmap& bitmap) {
  Gdiplus::Rect rect(0, 0, bitmap.GetWidth(), bitmap.GetHeight());
  Gdiplus::BitmapData data = {};
  Require(bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) == Gdiplus::Ok, "pixels");
  unsigned long long hash = 1469598103934665603ULL;
  for (UINT y = 0; y < bitmap.GetHeight(); ++y) {
    const BYTE* row = static_cast<const BYTE*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
    for (UINT x = 0; x < bitmap.GetWidth() * 4; ++x) hash = (hash ^ row[x]) * 1099511628211ULL;
  }
  bitmap.UnlockBits(&data); return hash;
}
static bool Denied(const wchar_t* path, DWORD desired) {
  HANDLE file = CreateFileW(path, desired, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
  if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); return false; }
  return GetLastError() == ERROR_ACCESS_DENIED;
}
static int Child(int argc, wchar_t** argv) {
  Require(argc == 6, "child arguments");
  HANDLE token = nullptr; DWORD app = 0, size = 0;
  Require(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != FALSE, "child token");
  Require(GetTokenInformation(token, TokenIsAppContainer, &app, sizeof(app), &size) != FALSE && app, "real AppContainer"); CloseHandle(token);
  weasel::SkinImageLoadError error;
  auto image = weasel::LoadSkinBitmap(argv[2], error);
  Require(image != nullptr && Pixels(*image) == std::stoull(argv[5]), "published image and alpha");
  Require(Denied(argv[2], GENERIC_WRITE) && Denied(argv[2], DELETE), "published image cannot be changed");
  const auto forbidden = fs::path(argv[2]).parent_path() / L"container-must-not-create.png";
  HANDLE created = CreateFileW(forbidden.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
  if (created != INVALID_HANDLE_VALUE) { CloseHandle(created); DeleteFileW(forbidden.c_str()); throw std::runtime_error("container can create cache files"); }
  Require(GetLastError() == ERROR_ACCESS_DENIED && Denied(argv[2], WRITE_DAC), "cache ACL denies creation and permission changes");
  auto source = weasel::LoadSkinBitmap(argv[3], error);
  Require(!source && error.file_error == ERROR_ACCESS_DENIED, "private source negative control");
  Require(Denied(argv[4], GENERIC_READ), "private dictionary remains inaccessible");
  return 0;
}
static std::wstring Quote(const fs::path& value) { return L"\"" + value.wstring() + L"\""; }
static void RunContainer(PSID sid, const fs::path& executable, const fs::path& image,
                         const fs::path& source, const fs::path& secret,
                         unsigned long long expected, bool restricted) {
  SIZE_T size = 0;
  InitializeProcThreadAttributeList(nullptr, restricted ? 2 : 1, 0, &size);
  std::vector<BYTE> attributes(size);
  auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
  Require(InitializeProcThreadAttributeList(list, restricted ? 2 : 1, 0, &size) != FALSE, "attributes");
  SECURITY_CAPABILITIES capabilities = {}; capabilities.AppContainerSid = sid;
  bool ok = UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &capabilities, sizeof(capabilities), nullptr, nullptr) != FALSE;
  DWORD policy = 1; // PROCESS_CREATION_ALL_APPLICATION_PACKAGES_OPT_OUT (LPAC).
  if (restricted) ok = ok && UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY, &policy, sizeof(policy), nullptr, nullptr) != FALSE;
  STARTUPINFOEXW start = {}; start.StartupInfo.cb = sizeof(start); start.lpAttributeList = list;
  std::wstring command = Quote(executable) + L" --child " + Quote(image) + L" " + Quote(source) + L" " + Quote(secret) + L" " + std::to_wstring(expected);
  PROCESS_INFORMATION process = {};
  if (ok) ok = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
      EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, executable.parent_path().c_str(), &start.StartupInfo, &process) != FALSE;
  DWORD error = GetLastError(); DeleteProcThreadAttributeList(list); SetLastError(error); Require(ok, "launch container");
  CloseHandle(process.hThread);
  DWORD wait = WaitForSingleObject(process.hProcess, 30000), exit = 999;
  if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 99);
  GetExitCodeProcess(process.hProcess, &exit); CloseHandle(process.hProcess);
  if (exit != 0) std::cerr << "container=" << (restricted ? "LPAC" : "AppContainer") << " exit=" << exit << '\n';
  Require(wait == WAIT_OBJECT_0 && exit == 0, restricted ? "LPAC decoder/access checks" : "AppContainer decoder/access checks");
}
int wmain(int argc, wchar_t** argv) {
  Gdiplus::GdiplusStartupInput input; ULONG_PTR gdip = 0;
  if (Gdiplus::GdiplusStartup(&gdip, &input, nullptr) != Gdiplus::Ok) return 1;
  int result = 0;
  try {
    if (argc > 1 && std::wstring(argv[1]) == L"--child") result = Child(argc, argv);
    else {
      Require(argc == 3, "arguments: PNG isolated-root");
      fs::path root = fs::absolute(argv[2]); fs::create_directories(root); Access(root, true);
      fs::path sourceDir = root / L"private"; fs::create_directory(sourceDir); Access(sourceDir, false);
      fs::path source = sourceDir / L"source.png", secret = root / L"Rime" / L"learning.userdb.txt";
      fs::copy_file(argv[1], source, fs::copy_options::overwrite_existing); Access(source, false);
      weasel::SkinAssetPublisher publisher(root / L"Rime" / L"SkinAssets" / L"v1",
                                           root / L"Rime" / L"SkinAssetState" / L"v1");
      std::vector<std::wstring> published; std::string error;
      Require(publisher.Publish({source.wstring()}, published, error), error.c_str());
      // Inherit from the common parent without fixing up this file's ACL: the
      // publisher must not make future unrelated Rime data container-readable.
      { std::ofstream stream(secret); stream << "isolated dummy dictionary"; }
      weasel::SkinImageLoadError load; auto bitmap = weasel::LoadSkinBitmap(source.c_str(), load);
      Require(bitmap != nullptr, "source bitmap"); auto expected = Pixels(*bitmap);
      wchar_t self[32768]; Require(GetModuleFileNameW(nullptr, self, 32768) != 0, "test executable");
      fs::path executable = root / L"decoder.exe";
      fs::copy_file(self, executable, fs::copy_options::overwrite_existing); Access(executable, true);
      std::wstring name = L"RimeSkinAssets.Test." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64());
      PSID sid = nullptr;
      Require(SUCCEEDED(CreateAppContainerProfile(name.c_str(), name.c_str(), L"Isolated PNG access regression", nullptr, 0, &sid)), "create test container");
      try {
        RunContainer(sid, executable, published[0], source, secret, expected, false);
        RunContainer(sid, executable, published[0], source, secret, expected, true);
      } catch (...) { FreeSid(sid); DeleteAppContainerProfile(name.c_str()); throw; }
      FreeSid(sid); Require(SUCCEEDED(DeleteAppContainerProfile(name.c_str())), "remove test container");
      std::cout << "PASS real AppContainer and LPAC, private-source negative control, cached PNG/alpha, read-only assets and private dictionary\n";
    }
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
  Gdiplus::GdiplusShutdown(gdip); return result;
}
