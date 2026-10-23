// Standard-library + Windows-header fixture for native lifetime regressions.
// No real registry, COM apartment, Explorer window, or file dialog is touched.
#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <strsafe.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

enum class QueryMode { Match, Failure, WrongType, Unterminated, OddLength };

static HKEY const kRootKey =
    reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(0x1001));
static HKEY const kSubKey =
    reinterpret_cast<HKEY>(static_cast<ULONG_PTR>(0x1002));
static ITEMIDLIST g_pidl_sentinel = {};

static int g_root_closes = 0;
static int g_sub_closes = 0;
static int g_invalid_closes = 0;
static QueryMode g_query_mode = QueryMode::Match;
static LSTATUS g_open_root_error = ERROR_SUCCESS;
static LSTATUS g_open_sub_error = ERROR_SUCCESS;

static HRESULT g_com_result = S_OK;
static bool g_pidl_failure = false;
static int g_create_calls = 0;
static int g_select_calls = 0;
static int g_free_calls = 0;
static int g_uninit_calls = 0;

static LSTATUS WINAPI MockRegOpenKeyExW(HKEY hKey, LPCWSTR, DWORD, REGSAM,
                                        PHKEY phkResult) {
  if (hKey == HKEY_LOCAL_MACHINE) {
    if (g_open_root_error != ERROR_SUCCESS)
      return g_open_root_error;
    *phkResult = kRootKey;
    return ERROR_SUCCESS;
  }
  if (hKey == kRootKey) {
    if (g_open_sub_error != ERROR_SUCCESS)
      return g_open_sub_error;
    *phkResult = kSubKey;
    return ERROR_SUCCESS;
  }
  return ERROR_INVALID_HANDLE;
}

static LSTATUS WINAPI MockRegQueryValueExW(HKEY, LPCWSTR, LPDWORD,
                                           LPDWORD lpType, LPBYTE lpData,
                                           LPDWORD lpcbData) {
  if (g_query_mode == QueryMode::Failure)
    return ERROR_FILE_NOT_FOUND;
  if (lpType == nullptr || lpData == nullptr || lpcbData == nullptr)
    return ERROR_INVALID_PARAMETER;

  const wchar_t kValue[] = L"weasel.ime";
  constexpr DWORD kChars = 10;
  constexpr DWORD kBytes = static_cast<DWORD>((kChars + 1) * sizeof(wchar_t));
  DWORD bytes = kBytes;
  if (g_query_mode == QueryMode::Unterminated)
    bytes = static_cast<DWORD>(kChars * sizeof(wchar_t));
  else if (g_query_mode == QueryMode::OddLength)
    bytes = static_cast<DWORD>(sizeof(wchar_t) + 1);
  if (bytes > *lpcbData)
    return ERROR_MORE_DATA;

  for (DWORD i = 0; i < bytes; ++i)
    lpData[i] = reinterpret_cast<const BYTE*>(kValue)[i];
  *lpType = (g_query_mode == QueryMode::WrongType) ? REG_DWORD : REG_SZ;
  *lpcbData = bytes;
  return ERROR_SUCCESS;
}

static LSTATUS WINAPI MockRegCloseKey(HKEY hKey) {
  if (hKey == kRootKey)
    ++g_root_closes;
  else if (hKey == kSubKey)
    ++g_sub_closes;
  else {
    ++g_invalid_closes;
    return ERROR_INVALID_HANDLE;
  }
  return ERROR_SUCCESS;
}

static HRESULT WINAPI MockCoInitializeEx(LPVOID, DWORD) {
  return g_com_result;
}

static void WINAPI MockCoUninitialize() {
  ++g_uninit_calls;
}

static PIDLIST_ABSOLUTE WINAPI MockILCreateFromPathW(LPCWSTR) {
  ++g_create_calls;
  return g_pidl_failure ? nullptr : &g_pidl_sentinel;
}

static void WINAPI MockILFree(PIDLIST_ABSOLUTE) {
  ++g_free_calls;
}

static HRESULT WINAPI MockSHOpenFolderAndSelectItems(
    PCIDLIST_ABSOLUTE pidl, UINT cidl, PCUITEMID_CHILD_ARRAY apidl, DWORD) {
  if (cidl != 0 || apidl != nullptr || pidl != &g_pidl_sentinel) {
    g_select_calls = -1;
    return E_INVALIDARG;
  }
  ++g_select_calls;
  return S_OK;
}

#define RegOpenKeyExW MockRegOpenKeyExW
#define RegQueryValueExW MockRegQueryValueExW
#define RegCloseKey MockRegCloseKey
#define CoInitializeEx MockCoInitializeEx
#define CoUninitialize MockCoUninitialize
#define ILCreateFromPathW MockILCreateFromPathW
#define ILFree MockILFree
#define SHOpenFolderAndSelectItems MockSHOpenFolderAndSelectItems

#include "find_ime_under_test.inc"
#include "shell_item_under_test.inc"

#undef RegOpenKeyExW
#undef RegQueryValueExW
#undef RegCloseKey
#undef CoInitializeEx
#undef CoUninitialize
#undef ILCreateFromPathW
#undef ILFree
#undef SHOpenFolderAndSelectItems

static void Check(bool ok, const char* message) {
  if (!ok)
    throw std::runtime_error(message);
}

static void ResetFindImeState() {
  g_root_closes = 0;
  g_sub_closes = 0;
  g_invalid_closes = 0;
  g_query_mode = QueryMode::Match;
  g_open_root_error = ERROR_SUCCESS;
  g_open_sub_error = ERROR_SUCCESS;
}

static int FindImeIterations(LANGID langid) {
  const DWORD first = 0xE0200000 | langid;
  const DWORD last = 0xE0FF0000 | langid;
  return static_cast<int>((last - first) / 0x10000 + 1);
}

static void TestFindIme() {
  const LANGID langid = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
  const DWORD first_id = 0xE0200000 | langid;
  const int iterations = FindImeIterations(langid);

  ResetFindImeState();
  g_open_root_error = ERROR_ACCESS_DENIED;
  Check(FindIME(langid) == nullptr, "FindIME root open failure returns null");
  Check(g_root_closes == 0 && g_sub_closes == 0,
        "FindIME root open failure closes nothing");
  Check(g_invalid_closes == 0,
        "FindIME root open failure closes no unknown key");

  ResetFindImeState();
  g_open_sub_error = ERROR_ACCESS_DENIED;
  Check(FindIME(langid) == nullptr, "FindIME subkey failures return null");
  Check(g_root_closes == 1 && g_sub_closes == 0,
        "FindIME subkey failures close only root");
  Check(g_invalid_closes == 0,
        "FindIME subkey failures close no unknown key");

  ResetFindImeState();
  const HKL matched = FindIME(langid);
  Check(matched != nullptr, "FindIME match returns HKL");
  Check(g_root_closes == 1 && g_sub_closes == 1,
        "FindIME match closes root and first subkey");
  Check(g_invalid_closes == 0, "FindIME match closes no unknown key");
  Check(reinterpret_cast<ULONG_PTR>(matched) ==
            static_cast<ULONG_PTR>(first_id),
        "FindIME preserves unsigned low 32-bit id");

  ResetFindImeState();
  g_query_mode = QueryMode::Failure;
  Check(FindIME(langid) == nullptr, "FindIME query failure returns null");
  Check(g_root_closes == 1 && g_sub_closes == iterations,
        "FindIME query failure closes every opened subkey");
  Check(g_invalid_closes == 0,
        "FindIME query failure closes no unknown key");

  ResetFindImeState();
  g_query_mode = QueryMode::WrongType;
  Check(FindIME(langid) == nullptr, "FindIME wrong type returns null");
  Check(g_root_closes == 1 && g_sub_closes == iterations,
        "FindIME wrong type closes every opened subkey");
  Check(g_invalid_closes == 0,
        "FindIME wrong type closes no unknown key");

  ResetFindImeState();
  g_query_mode = QueryMode::Unterminated;
  Check(FindIME(langid) == nullptr, "FindIME unterminated value returns null");
  Check(g_root_closes == 1 && g_sub_closes == iterations,
        "FindIME unterminated value closes every opened subkey");
  Check(g_invalid_closes == 0,
        "FindIME unterminated value closes no unknown key");

  ResetFindImeState();
  g_query_mode = QueryMode::OddLength;
  Check(FindIME(langid) == nullptr, "FindIME odd length returns null");
  Check(g_root_closes == 1 && g_sub_closes == iterations,
        "FindIME odd length closes every opened subkey");
  Check(g_invalid_closes == 0,
        "FindIME odd length closes no unknown key");
}

static void ResetShellState() {
  g_com_result = S_OK;
  g_pidl_failure = false;
  g_create_calls = 0;
  g_select_calls = 0;
  g_free_calls = 0;
  g_uninit_calls = 0;
}

static void TestShellItem() {
  const std::wstring path = L"E:\\code\\input\\sample.txt";

  ResetShellState();
  g_com_result = S_OK;
  OpenFolderAndSelectItem(path);
  Check(g_create_calls == 1 && g_select_calls == 1 && g_free_calls == 1 &&
            g_uninit_calls == 1,
        "S_OK opens, selects, frees, uninitializes");

  ResetShellState();
  g_com_result = S_FALSE;
  OpenFolderAndSelectItem(path);
  Check(g_create_calls == 1 && g_select_calls == 1 && g_free_calls == 1 &&
            g_uninit_calls == 1,
        "S_FALSE opens, selects, frees, uninitializes");

  ResetShellState();
  g_com_result = RPC_E_CHANGED_MODE;
  OpenFolderAndSelectItem(path);
  Check(g_create_calls == 1 && g_select_calls == 1 && g_free_calls == 1 &&
            g_uninit_calls == 0,
        "RPC_E_CHANGED_MODE proceeds without uninitialize");

  ResetShellState();
  g_com_result = E_OUTOFMEMORY;
  OpenFolderAndSelectItem(path);
  Check(g_create_calls == 0 && g_select_calls == 0 && g_free_calls == 0 &&
            g_uninit_calls == 0,
        "E_OUTOFMEMORY returns before PIDL and uninitialize");

  ResetShellState();
  g_com_result = S_OK;
  g_pidl_failure = true;
  OpenFolderAndSelectItem(path);
  Check(g_create_calls == 1 && g_select_calls == 0 && g_free_calls == 0 &&
            g_uninit_calls == 1,
        "PIDL failure still uninitializes on S_OK");
}

int main() {
  try {
    TestFindIme();
    TestShellItem();
    std::cout << "PASS native lifetime regressions\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
