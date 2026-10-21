// Compile the production CLI body against the real librime ABI. Win32 mutex
// and IPC are substituted: this test cannot touch the installed input service.
#include <windows.h>
#include <rime_api.h>
#include <rime_levers_api.h>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstring>

static void Require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
static std::vector<std::string> events;
static int failure = 0, connections = 0;
static DWORD mutex_error = 0;
static HANDLE FakeMutex(void*, BOOL, const wchar_t*) {
  events.push_back("mutex"); return failure == 1 ? nullptr : (HANDLE)1;
}
static DWORD FakeError() { return mutex_error; }
static BOOL FakeClose(HANDLE) { events.push_back("close"); return TRUE; }
#undef CreateMutex
#define CreateMutex FakeMutex
#define GetLastError FakeError
#define CloseHandle FakeClose
#define LOG(level) std::clog
namespace weasel {
struct Client {
  bool Connect() {
    ++connections;
    return failure != 2 && !(failure == 10 && connections == 2);
  }
  void StartMaintenance() { events.push_back("start"); }
  void EndMaintenance() { events.push_back("end"); }
};
}
static std::wstring u8tow(const std::string& value) {
  int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), nullptr, 0);
  std::wstring out(size, 0);
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), (int)value.size(), out.data(), size);
  return out;
}
static std::string wtou8(const std::wstring& value) {
  int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), nullptr, 0, nullptr, nullptr);
  std::string out(size, 0);
  WideCharToMultiByte(CP_UTF8, 0, value.data(), (int)value.size(), out.data(), size, nullptr, nullptr);
  return out;
}
static RimeApi rime = {};
static RimeLeversApi levers = {};
static RimeModule module = {};
extern "C" RimeApi* rime_get_api() { return &rime; }
struct Configurator {
  void Initialize() { events.push_back("initialize"); }
  int Run(bool install) { events.push_back(install ? "install" : "settings"); return 0; }
  int UpdateWorkspace() { events.push_back("deploy"); return 0; }
  int SyncUserData() { events.push_back("sync"); return 0; }
  int DictManagement() { events.push_back("gui"); return 0; }
  int DictManagement(const std::vector<std::wstring>&);
};
#include "dictionary_under_test.inc"
static int ShowHelp() { events.push_back("help"); return 0; }
#include "dispatch_under_test.inc"

static void Reset(int mode = 0) {
  events.clear(); failure = mode; connections = 0; mutex_error = 0;
  rime = {}; levers = {}; module = {};
  RIME_STRUCT_INIT(RimeApi, rime);
  RIME_STRUCT_INIT(RimeLeversApi, levers);
  RIME_STRUCT_INIT(RimeModule, module);
  module.get_api = []() -> RimeCustomApi* { return (RimeCustomApi*)&levers; };
  rime.find_module = [](const char*) -> RimeModule* { return failure == 3 ? nullptr : &module; };
  rime.run_task = [](const char* task) -> Bool {
    Require(std::string(task) == "installation_update", "wrong initialization task");
    events.push_back("task"); return failure != 4;
  };
  rime.get_user_data_sync_dir = [](char* buffer, size_t size) {
    if (failure == 11) return;
    strcpy_s(buffer, size, u8"C:\\测试 资料\\sync\\设备");
  };
  levers.user_dict_iterator_init = [](RimeUserDictIterator* iter) -> Bool {
    events.push_back("iterator"); iter->i = 0; return failure != 5;
  };
  levers.next_user_dict = [](RimeUserDictIterator* iter) -> const char* {
    if (failure == 6) throw std::runtime_error("enumeration failed");
    return iter->i++ ? nullptr : u8"工作";
  };
  levers.user_dict_iterator_destroy = [](RimeUserDictIterator*) { events.push_back("destroy"); };
  levers.backup_user_dict = [](const char* dict) -> Bool {
    Require(std::string(dict) == u8"工作", "dictionary UTF-8 lost");
    events.push_back("backup"); return failure != 7;
  };
  levers.restore_user_dict = [](const char* file) -> Bool {
    Require(std::string(file) == u8"C:\\测试 资料\\词典.txt", "snapshot argument split or corrupted");
    events.push_back("restore"); return failure != 7;
  };
  levers.export_user_dict = [](const char* dict, const char* file) -> int {
    Require(std::string(dict) == u8"工作" && std::string(file) == u8"C:\\测试 资料\\词典.txt", "export arguments lost");
    events.push_back("export"); return failure == 7 ? -1 : 0;
  };
  levers.import_user_dict = [](const char*, const char*) -> int {
    events.push_back("import"); return failure == 7 ? -1 : 12;
  };
}
struct Capture {
  std::wostringstream output, errors;
  std::wstreambuf* out = std::wcout.rdbuf(output.rdbuf());
  std::wstreambuf* err = std::wcerr.rdbuf(errors.rdbuf());
  ~Capture() { std::wcout.rdbuf(out); std::wcerr.rdbuf(err); }
};
int main() try {
  Configurator c;
  Capture capture;
  Reset(); Require(c.DictManagement({}) == 0 && events == std::vector<std::string>{"gui"}, "legacy GUI route changed");
  for (auto args : std::vector<std::vector<std::wstring>>{{L"help"}, {L"list", L"extra"}, {L"backup"}, {L"restore"}, {L"export", L"work"}, {L"import", L"work"}, {L"bad"}}) {
    Reset(); Require(c.DictManagement(args) == (args[0] == L"help" ? 0 : 2) && events.empty(), "usage touched maintenance or had wrong exit code");
  }
  Reset(); Require(c.DictManagement({L"LiSt"}) == 0, "case-insensitive list failed");
  Require(events == std::vector<std::string>{"mutex", "start", "task", "iterator", "destroy", "close", "end"}, "cleanup order or iterator ownership wrong");
  Require(capture.output.str().find(L"工作") != std::wstring::npos, "Unicode dictionary output lost");
  for (auto args : std::vector<std::vector<std::wstring>>{{L"backup", L"工作"}, {L"restore", L"C:\\测试 资料\\词典.txt"}, {L"export", L"工作", L"C:\\测试 资料\\词典.txt"}, {L"import", L"工作", L"C:\\测试 资料\\词典.txt"}}) {
    Reset(); Require(c.DictManagement(args) == 0, "valid dictionary command failed");
    Require(events[events.size()-2] == "close" && events.back() == "end", "service not resumed after success");
    Reset(7); Require(c.DictManagement(args) == 1 && events.back() == "end", "operation failure failed to resume service");
  }
  for (int mode : {3, 4, 5, 6, 10}) {
    Reset(mode); Require(c.DictManagement({L"list"}) == 1, "failure not reported");
    Require(mode == 10 ? events.back() == "close" : events.back() == "end", "failure cleanup not reached");
    if (mode == 6) Require(events[events.size()-3] == "destroy", "throw leaked iterator");
  }
  Reset(11); Require(c.DictManagement({L"backup", L"工作"}) == 1 && events.back() == "end", "missing sync dir returned bogus snapshot");
  Reset(); levers.next_user_dict = nullptr;
  Require(c.DictManagement({L"list"}) == 1 && events.back() == "end", "missing API crashed");
  Reset(); mutex_error = ERROR_ALREADY_EXISTS;
  Require(c.DictManagement({L"list"}) == 1 && events == std::vector<std::string>{"mutex", "close"}, "busy deployer touched service");
  Reset(1); Require(c.DictManagement({L"list"}) == 1 && events.size() == 1, "mutex creation failure ignored");
  Reset(2); Require(c.DictManagement({L"list"}) == 0 && events.back() == "close", "absent service prevented offline dictionary tools");
  for (std::wstring option : {L"/deploy", L"/sync", L"/install", L"/dict", L"/help"}) {
    Reset(); wchar_t* argv[] = {const_cast<wchar_t*>(L"deployer"), option.data()};
    Require(!IsDictionaryCli(2, argv), "GUI command classified as CLI");
    Require(Run(2, argv) == 0, "GUI dispatch failed");
    std::string expected = option == L"/deploy" ? "deploy" : option == L"/sync" ? "sync" : option == L"/install" ? "install" : option == L"/dict" ? "gui" : "help";
    Require(events.back() == expected, "legacy entry redirected to wrong operation");
  }
  Reset(); wchar_t* cli[] = {const_cast<wchar_t*>(L"deployer"), const_cast<wchar_t*>(L"/DICT"), const_cast<wchar_t*>(L"list")};
  Require(IsDictionaryCli(3, cli) && Run(3, cli) == 0, "dictionary subcommand dispatch failed");
  std::cout << "PASS production dictionary CLI: Unicode, commands, busy/API/task failures, iterator and maintenance cleanup\n";
  return 0;
} catch (const std::exception& error) {
  std::clog << "FAIL " << error.what() << '\n'; return 1;
}
