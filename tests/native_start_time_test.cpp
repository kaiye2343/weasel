// Standard-library-only fixture for native start and current_time regressions.
// tools/test-native-start-time.ps1 injects the production functions at the
// two markers below. No Windows API is called.
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using HWND = void*;
using HANDLE = void*;
using DWORD = unsigned long;

constexpr DWORD ERROR_ALREADY_EXISTS = 183;
constexpr DWORD ERROR_ACCESS_DENIED = 5;
constexpr int FALSE = 0;

#ifdef NULL
#undef NULL
#endif
#define NULL nullptr

namespace {
HWND g_created_window = nullptr;
int g_create_calls = 0;
DWORD g_last_error = 0;
}  // namespace

// Controlled stand-ins for the production calls. The mutex is only a sentinel:
// the fixture never creates or releases a real kernel object.
inline HANDLE CreateMutex(void*, int, const wchar_t*) {
  return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(1));
}

inline DWORD GetLastError() {
  return g_last_error;
}

inline std::wstring getUsername() {
  return L"fixture";
}

class ServerImpl {
 public:
  int Start();
  HWND Create(std::nullptr_t);
};

HWND ServerImpl::Create(std::nullptr_t) {
  ++g_create_calls;
  return g_created_window;
}

#pragma warning(push)
#pragma warning(disable : 4189)  // production keeps the mutex handle alive unread
// PRODUCTION_START
#pragma warning(pop)

std::atomic<bool> g_time_failure{false};

int TestLocalTimeS(std::tm* out, const std::time_t*) {
  if (g_time_failure.load(std::memory_order_relaxed)) {
    return 1;
  }
  out->tm_year = 124;
  out->tm_mon = 10;
  out->tm_mday = 13;
  out->tm_hour = 8;
  out->tm_min = 54;
  out->tm_sec = 34;
  return 0;
}

#define localtime_s TestLocalTimeS
// PRODUCTION_TIME
#undef localtime_s

namespace {
int Fail(const char* reason) {
  std::cerr << "FAIL: " << reason << std::endl;
  return 1;
}

int TestStart() {
  ServerImpl server;

  g_created_window = nullptr;
  g_create_calls = 0;
  g_last_error = 0;
  if (server.Start() != 0) {
    return Fail("Start must return 0 when Create returns a null window");
  }
  if (g_create_calls != 1) {
    return Fail("Start must call Create once for the null window case");
  }

  std::uint64_t high = 0x12345678u;
  high <<= 32;
  std::uintptr_t raw = static_cast<std::uintptr_t>(high);
  if constexpr (sizeof(std::uintptr_t) < 8) {
    raw = static_cast<std::uintptr_t>(0x12345678u);
  }
  g_created_window = reinterpret_cast<HWND>(raw);
  g_create_calls = 0;
  g_last_error = 0;
  if (server.Start() != 1) {
    return Fail("Start must return 1 when Create returns a non-null window");
  }
  if (g_create_calls != 1) {
    return Fail("Start must call Create once for the non-null window case");
  }

  g_created_window = reinterpret_cast<HWND>(raw);
  g_create_calls = 0;
  g_last_error = ERROR_ALREADY_EXISTS;
  if (server.Start() != 0) {
    return Fail("Start must return 0 when the mutex already exists");
  }
  if (g_create_calls != 0) {
    return Fail("Start must not call Create when the mutex already exists");
  }

  g_created_window = reinterpret_cast<HWND>(raw);
  g_create_calls = 0;
  g_last_error = ERROR_ACCESS_DENIED;
  if (server.Start() != 0) {
    return Fail("Start must return 0 when the mutex is access denied");
  }
  if (g_create_calls != 0) {
    return Fail("Start must not call Create when the mutex is access denied");
  }

  return 0;
}

int TestCurrentTime() {
  const std::regex pattern(R"(^20241113 08:54:34\.[0-9]{6}$)");

  g_time_failure.store(false, std::memory_order_relaxed);
  const std::string normal = current_time();
  if (!std::regex_match(normal, pattern)) {
    std::cerr << "unexpected current_time value: " << normal << std::endl;
    return Fail("current_time must format the fixed local time with microseconds");
  }

  g_time_failure.store(true, std::memory_order_relaxed);
  const std::string failed = current_time();
  if (failed != "time-unavailable") {
    std::cerr << "unexpected failure value: " << failed << std::endl;
    return Fail("current_time must return time-unavailable on localtime_s failure");
  }
  g_time_failure.store(false, std::memory_order_relaxed);

  std::atomic<int> failures{0};
  std::vector<std::thread> threads;
  threads.reserve(8);
  for (int i = 0; i < 8; ++i) {
    threads.emplace_back([&failures]() {
      const std::regex thread_pattern(R"(^20241113 08:54:34\.[0-9]{6}$)");
      for (int j = 0; j < 100; ++j) {
        if (!std::regex_match(current_time(), thread_pattern)) {
          ++failures;
        }
      }
    });
  }
  for (auto& worker : threads) {
    worker.join();
  }
  if (failures.load() != 0) {
    return Fail("current_time must remain valid under concurrent calls");
  }
  return 0;
}
}  // namespace

int main() {
  const int start_result = TestStart();
  if (start_result != 0) {
    return start_result;
  }
  return TestCurrentTime();
}
