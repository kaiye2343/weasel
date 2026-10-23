// Standard-library + Windows-header fixture for RegisterProfiles contracts.
// No real COM, registry, TSF registration, or product process is used.
#include <windows.h>
#include <objbase.h>
#include <string>
#include <limits>
#include <stdexcept>
#include <iostream>
#include <cwchar>
#include <vector>

const GUID c_clsidTextService = {};
const GUID c_guidProfile = {};
const GUID CLSID_TF_InputProcessorProfiles = {};

HINSTANCE g_hInst = nullptr;

constexpr ULONG TEXTSERVICE_ICON_INDEX = 0;
constexpr LANGID TEXTSERVICE_LANGID_HANS = 0x0804;
constexpr LANGID TEXTSERVICE_LANGID_HANT = 0x0404;
constexpr LANGID TEXTSERVICE_LANGID_HONGKONG = 0x0C04;
constexpr LANGID TEXTSERVICE_LANGID_MACAU = 0x1404;
constexpr LANGID TEXTSERVICE_LANGID_SINGAPORE = 0x1004;

std::wstring g_description = L"小狼毫扩展版";
int g_registrations = 0;
HRESULT g_create_result = S_OK;
HRESULT g_register_result = S_OK;

static void Check(bool ok, const char* message) {
  if (!ok)
    throw std::runtime_error(message);
}

static std::wstring get_weasel_ime_name() {
  return g_description;
}

static HKL FindIME(LANGID) {
  return nullptr;
}

static DWORD MockGetEnvironmentVariable(LPCWSTR, LPWSTR buffer, DWORD size) {
  wcscpy_s(buffer, static_cast<size_t>(size), L"hans");
  return 4;
}

static DWORD MockGetModuleFileNameW(HMODULE, LPWSTR buffer, DWORD size) {
  wcscpy_s(buffer, static_cast<size_t>(size), L"fixture.dll");
  return 11;
}

struct ITfInputProcessorProfileMgr {
  HRESULT RegisterProfile(REFCLSID, LANGID, REFGUID, LPCWSTR desc, ULONG cch,
                          LPCWSTR icon, ULONG iconChars, ULONG, HKL, DWORD,
                          BOOL, DWORD) {
    Check(desc != nullptr, "description pointer");
    Check(cch == g_description.size(), "description length");
    Check(std::wstring(desc, cch) == g_description, "description text");
    Check(desc[cch] == L'\0', "description terminator");
    Check(icon != nullptr, "icon pointer");
    Check(iconChars == 11, "icon length");
    Check(std::wstring(icon, iconChars) == L"fixture.dll", "icon text");
    ++g_registrations;
    return g_register_result;
  }
};

template <class T>
class CComPtr {
 public:
  HRESULT CoCreateInstance(REFCLSID, LPUNKNOWN, DWORD) {
    return g_create_result;
  }
  T* operator->() { return &item_; }

 private:
  T item_{};
};

#ifdef GetEnvironmentVariable
#undef GetEnvironmentVariable
#endif
#define GetEnvironmentVariable MockGetEnvironmentVariable
#define GetModuleFileNameW MockGetModuleFileNameW

#include "profiles_under_test.inc"

#undef GetEnvironmentVariable
#undef GetModuleFileNameW

static void ResetProfileState(const wchar_t* description) {
  g_description = description;
  g_registrations = 0;
  g_create_result = S_OK;
  g_register_result = S_OK;
}

int main() {
  try {
    ResetProfileState(L"小狼毫扩展版");
    Check(RegisterProfiles() == TRUE, "Chinese description registration");
    Check(g_registrations == 5, "Chinese description registers five profiles");

    ResetProfileState(L"Weasel");
    Check(RegisterProfiles() == TRUE, "ASCII description registration");
    Check(g_registrations == 5, "ASCII description registers five profiles");

    ResetProfileState(L"");
    Check(RegisterProfiles() == TRUE, "empty description registration");
    Check(g_registrations == 5, "empty description registers five profiles");

    ResetProfileState(L"Weasel");
    g_create_result = E_FAIL;
    Check(RegisterProfiles() == FALSE, "create failure returns false");
    Check(g_registrations == 0, "create failure registers nothing");

    ResetProfileState(L"Weasel");
    g_register_result = E_FAIL;
    Check(RegisterProfiles() == FALSE, "register failure returns false");
    Check(g_registrations == 1, "register failure stops after first profile");

    std::cout << "PASS native profile contracts\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
