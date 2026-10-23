// Standard-library + project-header fixture for native IPC bounds regressions.
// No real pipe, window, service, registry, or Rime data is touched.
#include <WeaselIPCConversions.h>
#include <PipeChannel.h>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "commands_under_test.inc"

static bool g_active = false;
static DWORD g_reply = 0;
static int g_calls = 0;
static DWORD g_last_index = 0;
static WEASEL_IPC_COMMAND g_last_command = WEASEL_IPC_LAST_COMMAND;
static int g_client_info_writes = 0;

class TestChannel : public weasel::PipeChannel<DWORD> {
 public:
  using weasel::PipeChannel<DWORD>::PipeChannel;
  using weasel::PipeChannel<DWORD>::_WritePipe;
  using weasel::PipeChannel<DWORD>::_Receive;
};

namespace weasel {

class ClientImpl {
 public:
  bool _Active() const { return g_active; }

  DWORD _SendMessage(WEASEL_IPC_COMMAND cmd, DWORD wParam, DWORD lParam) {
    (void)lParam;
    ++g_calls;
    g_last_command = cmd;
    g_last_index = wParam;
    return g_reply;
  }

  bool _WriteClientInfo() {
    ++g_client_info_writes;
    return true;
  }

  UINT session_id = 0;

  bool SelectCandidateOnCurrentPage(size_t index);
  bool HighlightCandidateOnCurrentPage(size_t index);
  void StartSession();
  bool Echo();
};

// PRODUCTION_CLIENT_METHODS
#include "client_methods.inc"

}  // namespace weasel

static void Check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

static void ResetClientState() {
  g_active = true;
  g_reply = 1;
  g_calls = 0;
  g_last_index = 0;
  g_last_command = WEASEL_IPC_LAST_COMMAND;
  g_client_info_writes = 0;
}

static void TestConversions() {
  Check(weasel::CheckedPipeSize(0) == 0, "CheckedPipeSize(0)");
  const DWORD dword_max = (std::numeric_limits<DWORD>::max)();
  Check(weasel::CheckedPipeSize(static_cast<size_t>(dword_max)) == dword_max,
        "CheckedPipeSize(DWORD_MAX)");

  DWORD out = 0;
  Check(weasel::TryProtocolIndex(0, out) && out == 0, "TryProtocolIndex(0)");
  Check(weasel::TryProtocolIndex(static_cast<size_t>(dword_max), out) &&
            out == dword_max,
        "TryProtocolIndex(DWORD_MAX)");

  if constexpr (sizeof(size_t) > sizeof(DWORD)) {
    volatile size_t dword_max_runtime = dword_max;
    const size_t too_large = dword_max_runtime + 1;
    bool threw = false;
    try {
      (void)weasel::CheckedPipeSize(too_large);
    } catch (DWORD error) {
      threw = true;
      Check(error == ERROR_ARITHMETIC_OVERFLOW, "CheckedPipeSize error code");
    }
    Check(threw, "CheckedPipeSize must throw beyond DWORD");

    DWORD sentinel = 0xDEADBEEFu;
    Check(!weasel::TryProtocolIndex(too_large, sentinel),
          "TryProtocolIndex must reject beyond DWORD");
    Check(sentinel == 0xDEADBEEFu,
          "TryProtocolIndex must not modify out on failure");
  }
}

static void TestSessionIdConversions() {
  const DWORD zero_extended[] = {0u, 0x7fffffffu, 0x80000000u, 0xffffffffu};
  for (DWORD expected : zero_extended) {
    const ULONG_PTR raw = static_cast<ULONG_PTR>(expected);
    DWORD value = 0xDEADBEEFu;
    Check(weasel::TryProtocolSessionId(static_cast<LPARAM>(raw), value),
          "zero-extended session id accepted");
    Check(value == expected, "zero-extended session id preserved");
  }

  if constexpr (sizeof(ULONG_PTR) > sizeof(DWORD)) {
    const ULONG_PTR mask =
        static_cast<ULONG_PTR>((std::numeric_limits<DWORD>::max)());
    DWORD value = 0xDEADBEEFu;
    Check(weasel::TryProtocolSessionId(static_cast<LPARAM>(-1), value),
          "sign-extended -1 session id accepted");
    Check(value == 0xffffffffu, "sign-extended -1 preserved");

    value = 0xDEADBEEFu;
    Check(weasel::TryProtocolSessionId(
              static_cast<LPARAM>(-2147483647 - 1), value),
          "sign-extended INT_MIN session id accepted");
    Check(value == 0x80000000u, "sign-extended INT_MIN preserved");

    volatile ULONG_PTR mask_runtime = mask;
    const ULONG_PTR high = mask_runtime + 1;
    const ULONG_PTR invalid_high = high | 1;
    const ULONG_PTR invalid_sign = (~mask_runtime) | 1;

    DWORD sentinel = 0xDEADBEEFu;
    Check(!weasel::TryProtocolSessionId(
              static_cast<LPARAM>(invalid_high), sentinel),
          "non-sign-extended high id rejected");
    Check(sentinel == 0xDEADBEEFu, "rejected high id keeps sentinel");

    sentinel = 0xDEADBEEFu;
    Check(!weasel::TryProtocolSessionId(
              static_cast<LPARAM>(invalid_sign), sentinel),
          "invalid sign extension rejected");
    Check(sentinel == 0xDEADBEEFu,
          "rejected sign extension keeps sentinel");
  }
}

static void TestChannelBounds() {
  if constexpr (sizeof(size_t) > sizeof(DWORD)) {
    volatile size_t dword_max_runtime = (std::numeric_limits<DWORD>::max)();
    const size_t too_large = dword_max_runtime + 1;
    TestChannel channel(std::wstring(L"unused-native-ipc-bounds"), nullptr, 4096);

    bool threw = false;
    try {
      TestChannel bad(std::wstring(L"unused-native-ipc-bounds"), nullptr,
                      too_large);
      (void)bad;
    } catch (DWORD error) {
      threw = true;
      Check(error == ERROR_ARITHMETIC_OVERFLOW,
            "PipeChannel constructor error code");
    }
    Check(threw, "PipeChannel constructor must throw beyond DWORD");

    threw = false;
    try {
      channel._WritePipe(INVALID_HANDLE_VALUE, too_large, nullptr);
    } catch (DWORD error) {
      threw = true;
      Check(error == ERROR_ARITHMETIC_OVERFLOW, "_WritePipe error code");
    }
    Check(threw, "_WritePipe must throw before WriteFile");

    threw = false;
    try {
      channel._Receive(INVALID_HANDLE_VALUE, nullptr, too_large);
    } catch (DWORD error) {
      threw = true;
      Check(error == ERROR_ARITHMETIC_OVERFLOW, "_Receive error code");
    }
    Check(threw, "_Receive must throw before ReadFile");
  }
}

static void TestSelectCandidate() {
  weasel::ClientImpl client;
  ResetClientState();
  client.session_id = 0x12345678u;
  Check(client.SelectCandidateOnCurrentPage(7), "select returns true");
  Check(g_calls == 1, "select sends once");
  Check(g_last_command == WEASEL_IPC_SELECT_CANDIDATE_ON_CURRENT_PAGE,
        "select command");
  Check(g_last_index == 7u, "select preserves index");

  g_reply = 0;
  Check(!client.SelectCandidateOnCurrentPage(8), "select zero reply false");
  Check(g_calls == 2 && g_last_index == 8u, "select zero reply sends index");

  g_reply = 1;
  const int calls_before = g_calls;
  const DWORD index_before = g_last_index;
  if constexpr (sizeof(size_t) > sizeof(DWORD)) {
    const size_t too_large = (std::numeric_limits<size_t>::max)();
    Check(!client.SelectCandidateOnCurrentPage(too_large),
          "select rejects beyond DWORD");
    Check(g_calls == calls_before && g_last_index == index_before,
          "select beyond DWORD must not send");
  }

  g_active = false;
  Check(!client.SelectCandidateOnCurrentPage(9), "select inactive false");
  Check(g_calls == calls_before, "select inactive must not send");
}

static void TestHighlightCandidate() {
  weasel::ClientImpl client;
  ResetClientState();
  client.session_id = 0x12345678u;
  Check(client.HighlightCandidateOnCurrentPage(3), "highlight returns true");
  Check(g_calls == 1, "highlight sends once");
  Check(g_last_command == WEASEL_IPC_HIGHLIGHT_CANDIDATE_ON_CURRENT_PAGE,
        "highlight command");
  Check(g_last_index == 3u, "highlight preserves index");

  g_reply = 0;
  Check(!client.HighlightCandidateOnCurrentPage(4),
        "highlight zero reply false");
  Check(g_calls == 2 && g_last_index == 4u, "highlight zero reply sends index");

  g_reply = 1;
  const int calls_before = g_calls;
  const DWORD index_before = g_last_index;
  if constexpr (sizeof(size_t) > sizeof(DWORD)) {
    const size_t too_large = (std::numeric_limits<size_t>::max)();
    Check(!client.HighlightCandidateOnCurrentPage(too_large),
          "highlight rejects beyond DWORD");
    Check(g_calls == calls_before && g_last_index == index_before,
          "highlight beyond DWORD must not send");
  }

  g_active = false;
  Check(!client.HighlightCandidateOnCurrentPage(5), "highlight inactive false");
  Check(g_calls == calls_before, "highlight inactive must not send");
}

static void TestStartSession() {
  weasel::ClientImpl client;
  ResetClientState();
  g_active = false;
  g_reply = 0x80000001u;
  client.session_id = 0;
  client.StartSession();
  Check(client.session_id == 0x80000001u, "StartSession preserves high-bit id");
  Check(g_calls == 1, "StartSession sends once");
  Check(g_last_command == WEASEL_IPC_START_SESSION, "StartSession command");
  Check(g_client_info_writes == 1, "StartSession writes client info");

  ResetClientState();
  g_reply = 0x80000001u;
  client.session_id = 0x80000001u;
  client.StartSession();
  Check(client.session_id == 0x80000001u, "StartSession keeps active session");
  Check(g_calls == 1, "StartSession active echo sends once");
  Check(g_last_command == WEASEL_IPC_ECHO, "StartSession active echo command");
  Check(g_client_info_writes == 0, "StartSession active echo skips client info");

  ResetClientState();
  g_reply = 0x80000002u;
  client.session_id = 0x80000001u;
  client.StartSession();
  Check(client.session_id == 0x80000002u, "StartSession replaces stale session");
  Check(g_calls == 2, "StartSession stale echo then start sends twice");
  Check(g_client_info_writes == 1, "StartSession stale writes client info");
}

static void TestEcho() {
  weasel::ClientImpl client;
  ResetClientState();
  client.session_id = 0xffffffffu;
  g_reply = 0xffffffffu;
  Check(client.Echo(), "Echo matches high-bit session");
  Check(g_calls == 1 && g_last_command == WEASEL_IPC_ECHO, "Echo command");

  g_reply = 0xfffffffeu;
  Check(!client.Echo(), "Echo mismatch false");
  Check(g_calls == 2, "Echo mismatch sends");

  g_active = false;
  Check(!client.Echo(), "Echo inactive false");
  Check(g_calls == 2, "Echo inactive must not send");
}

int main() {
  try {
    TestConversions();
    TestSessionIdConversions();
    TestChannelBounds();
    TestSelectCandidate();
    TestHighlightCandidate();
    TestStartSession();
    TestEcho();
    std::cout << "PASS native IPC bounds and client conversions\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
