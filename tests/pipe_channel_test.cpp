#include <PipeChannel.h>
#include <atomic>
#include <iostream>
#include <vector>
#include <cwchar>

class Channel : public weasel::PipeChannel<DWORD> {
 public:
  using weasel::PipeChannel<DWORD>::PipeChannel;
  void Own(HANDLE handle) { *_GetPipeHandle() = handle; }
  DWORD SendWithStaleHandle(DWORD value) {
    _Send(INVALID_HANDLE_VALUE, value);
    return _ReceiveResponse();
  }
};

static void Fail(const char* message) {
  std::cerr << message << " error=" << GetLastError() << '\n';
  ExitProcess(3); // Also terminates a blocked fixture worker on failure.
}

static void RoundTrip(bool stale) {
  const std::wstring name = L"\\\\.\\pipe\\WeaselRegression-" +
      std::to_wstring(GetCurrentProcessId()) + (stale ? L"-reconnect" : L"-body");
  auto make = [&]() {
    HANDLE pipe = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES, 65536, 65536, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) Fail("CreateNamedPipe");
    return pipe;
  };
  HANDLE first = make(), second = stale ? make() : INVALID_HANDLE_VALUE;
  auto connect = [](HANDLE pipe) {
    if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) Fail("ConnectNamedPipe");
  };
  const std::wstring body = L"isolated payload \u5b57\u5178";
  boost::thread server([&]() {
    connect(first);
    HANDLE active = first;
    if (stale) {
      DWORD value = 0, count = 0;
      if (ReadFile(first, &value, sizeof(value), &count, nullptr) || GetLastError() != ERROR_BROKEN_PIPE)
        Fail("Old connection must close before reconnect");
      CloseHandle(first); active = second; connect(active);
    }
    alignas(DWORD) char bytes[65536] = {}; DWORD count = 0;
    if (!ReadFile(active, bytes, sizeof(bytes), &count, nullptr)) Fail("Read payload");
    const DWORD expected = sizeof(DWORD) + (stale ? 0 : DWORD((body.size() + 1) * sizeof(wchar_t)));
    if (count != expected || *reinterpret_cast<DWORD*>(bytes) != 42) Fail("Packet size/header");
    if (!stale && std::wcscmp(reinterpret_cast<wchar_t*>(bytes + sizeof(DWORD)), body.c_str())) Fail("Payload bytes");
    DWORD result = 43, written = 0;
    if (!WriteFile(active, &result, sizeof(result), &written, nullptr) || written != sizeof(result)) Fail("Write response");
    FlushFileBuffers(active); CloseHandle(active);
  });
  Channel channel{std::wstring(name)};
  try {
    if (!channel.Connect()) Fail("Connect client");
    DWORD result = 0;
    if (stale) result = channel.SendWithStaleHandle(42);
    else { channel << body << L'\0'; DWORD value = 42; result = channel.Transact(value); }
    if (result != 43) Fail("Response value");
    channel.Disconnect();
  } catch (...) { Fail("Client transaction threw"); }
  server.join();
  std::cout << "PASS " << (stale ? "stale handle reconnect" : "exact UTF-16 payload size and round trip") << '\n';
}

static bool ThreadIsolation() {
  Channel channel{std::wstring(L"unused-fixture")};
  channel << L"main";
  constexpr size_t count = 8;
  std::atomic<size_t> ready{0}; std::atomic<bool> valid{true};
  std::vector<HANDLE> owned(count); std::vector<boost::thread> workers;
  for (size_t i = 0; i < count; ++i) workers.emplace_back([&, i]() {
    const auto value = L"worker-" + std::to_wstring(i);
    channel << value;
    owned[i] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!owned[i]) Fail("CreateEvent");
    channel.Own(owned[i]); ++ready;
    while (ready != count) Sleep(1);
    if (std::wcscmp(reinterpret_cast<wchar_t*>(channel.SendBuffer()), value.c_str())) valid = false;
  });
  for (auto& worker : workers) worker.join();
  if (std::wcscmp(reinterpret_cast<wchar_t*>(channel.SendBuffer()), L"main")) valid = false;
  for (auto handle : owned) {
    DWORD flags = 0;
    if (GetHandleInformation(handle, &flags)) { valid = false; CloseHandle(handle); }
    else if (GetLastError() != ERROR_INVALID_HANDLE) valid = false;
  }
  std::cout << "thread buffers isolated and handles released=" << valid << '\n';
  return valid;
}

int wmain(int argc, wchar_t** argv) {
  const bool reconnectOnly = argc == 2 && !std::wcscmp(argv[1], L"--reconnect-only");
  const bool threadsOnly = argc == 2 && !std::wcscmp(argv[1], L"--threads-only");
  if (!reconnectOnly && !ThreadIsolation()) return 4;
  if (!threadsOnly) { RoundTrip(false); RoundTrip(true); }
  return 0;
}
