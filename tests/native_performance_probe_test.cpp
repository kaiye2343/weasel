// Standard-library-only regression tests for include/WeaselPerformance.h.
// No window, service, registry, real configuration, or Rime data is touched.

#include <WeaselPerformance.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <locale>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

static thread_local bool failNextAllocation = false;
static std::atomic<std::uint64_t> allocationCount{0};

void* operator new(std::size_t bytes) {
  if (failNextAllocation) {
    failNextAllocation = false;
    throw std::bad_alloc();
  }
  void* pointer = std::malloc(bytes ? bytes : 1);
  if (pointer == nullptr) {
    throw std::bad_alloc();
  }
  allocationCount.fetch_add(1, std::memory_order_relaxed);
  return pointer;
}

void* operator new[](std::size_t bytes) {
  return ::operator new(bytes);
}

void operator delete(void* pointer) noexcept {
  std::free(pointer);
}

void operator delete[](void* pointer) noexcept {
  std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
  std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
  std::free(pointer);
}

namespace perf = weasel::performance;

static std::atomic<perf::Tick> fakeNow{0};
static std::atomic<std::uint64_t> fakeClockCalls{0};

static perf::Tick FakeNow() noexcept {
  fakeClockCalls.fetch_add(1, std::memory_order_relaxed);
  return fakeNow.load(std::memory_order_relaxed);
}

static void Check(bool ok, const char* message) {
  if (!ok) {
    throw std::runtime_error(message);
  }
}

static std::size_t StageIndex(perf::Stage stage) {
  return static_cast<std::size_t>(stage);
}

static std::size_t CounterIndex(perf::Counter counter) {
  return static_cast<std::size_t>(counter);
}

static perf::Options BaseOptions() {
  perf::Options options;
  options.sourceRevision = "unknown";
  options.pid = 0;
  options.backend = "gdi";
  options.dpi = 96;
  options.scenarioId = "synthetic";
  options.durationSeconds = 30;
  options.available.fill(false);
  options.available[StageIndex(perf::Stage::Layout)] = true;
  return options;
}

static void TestClosedFastPath() {
  fakeNow.store(0);
  fakeClockCalls.store(0);

  perf::Probe probe(&FakeNow);
  Check(!probe.Enabled(), "probe starts disabled");
  const auto allocationsBefore =
      allocationCount.load(std::memory_order_relaxed);

  for (int iteration = 0; iteration < 1000000; ++iteration) {
    perf::Token token = probe.Begin(perf::Stage::Layout);
    Check(token.generation == 0u, "disabled Begin returns an empty token");
    probe.End(token);
    probe.Add(perf::Counter::FileMetadata, 1);
  }
  for (int iteration = 0; iteration < 100000; ++iteration) {
    perf::Scope scope(probe, perf::Stage::Layout);
    (void)scope;
  }

  Check(allocationCount.load(std::memory_order_relaxed) == allocationsBefore,
        "disabled fast path does not allocate");
  Check(fakeClockCalls.load(std::memory_order_relaxed) == 0u,
        "disabled fast path never calls the clock");
  Check(!probe.Enabled(), "probe stays disabled");

  perf::Report report;
  report.durationMs = 123.0;
  report.stages[0].samples = 7;
  report.counters[0] = 9;
  std::string error = "unchanged";
  Check(!probe.Finish(report, error), "Finish without a session fails");
  Check(report.durationMs == 123.0, "failed Finish keeps durationMs");
  Check(report.stages[0].samples == 7u, "failed Finish keeps stage data");
  Check(report.counters[0] == 9u, "failed Finish keeps counters");
  Check(!error.empty(), "failed Finish reports an error");
}

static void ExpectStartFails(const perf::Options& options, const char* message) {
  fakeNow.store(0);
  perf::Probe probe(&FakeNow);
  std::string error;
  Check(!probe.Start(options, error), message);
  Check(!error.empty(), "failed Start sets an English error");
  Check(!probe.Enabled(), "failed Start leaves the probe disabled");
}

static void ExpectStartOk(const perf::Options& options, const char* message) {
  fakeNow.store(0);
  perf::Probe probe(&FakeNow);
  std::string error = "stale";
  Check(probe.Start(options, error), message);
  Check(error.empty(), "successful Start clears the error");
  perf::Report report;
  Check(probe.Finish(report, error), "Finish after a successful Start");
  Check(error.empty(), "successful Finish clears the error");
}

static void TestValidation() {
  const perf::Options base = BaseOptions();

  const auto withDuration = [&base](std::uint32_t value) {
    perf::Options options = base;
    options.durationSeconds = value;
    return options;
  };
  const auto withRevision = [&base](std::string value) {
    perf::Options options = base;
    options.sourceRevision = std::move(value);
    return options;
  };
  const auto withScenario = [&base](std::string value) {
    perf::Options options = base;
    options.scenarioId = std::move(value);
    return options;
  };
  const auto withBackend = [&base](std::string value) {
    perf::Options options = base;
    options.backend = std::move(value);
    return options;
  };
  const auto withBitness = [&base](std::uint32_t value) {
    perf::Options options = base;
    options.bitness = value;
    return options;
  };
  const auto withDpi = [&base](std::uint32_t value) {
    perf::Options options = base;
    options.dpi = value;
    return options;
  };
  const auto withPid = [&base](std::uint32_t value) {
    perf::Options options = base;
    options.pid = value;
    return options;
  };
  const auto withNoStages = [&base]() {
    perf::Options options = base;
    options.available.fill(false);
    return options;
  };

  ExpectStartOk(withDuration(30), "default 30 second duration is valid");
  ExpectStartOk(withDuration(120), "maximum 120 second duration is valid");
  ExpectStartFails(withDuration(0), "zero duration is rejected");
  ExpectStartFails(withDuration(121), "duration above 120 is rejected");

  ExpectStartFails(withRevision("C:\\Users\\example\\config"),
                   "path-like sourceRevision is rejected");
  ExpectStartFails(withRevision(std::string(39, 'a')),
                   "39 character revision is rejected");
  ExpectStartFails(withRevision(std::string(41, 'a')),
                   "41 character revision is rejected");
  ExpectStartFails(withRevision(std::string(40, 'g')),
                   "non-hex revision is rejected");
  ExpectStartOk(withRevision(std::string(40, 'a')),
                "40 hex character revision is valid");
  ExpectStartOk(withRevision("unknown"), "unknown revision is valid");

  ExpectStartFails(withScenario("../escape"), "path-like scenario is rejected");
  ExpectStartFails(withScenario("bad scenario"), "space in scenario is rejected");
  ExpectStartFails(withScenario(""), "empty scenario is rejected");
  ExpectStartFails(withScenario(std::string(65, 'a')),
                   "65 character scenario is rejected");
  ExpectStartOk(withScenario(std::string(64, 'a')),
                "64 safe scenario characters are valid");
  ExpectStartOk(withScenario("layout.run_1-2"), "safe scenario is valid");

  ExpectStartFails(withBackend("opengl"), "unknown backend is rejected");
  ExpectStartOk(withBackend("dcomp"), "dcomp backend is valid");
  ExpectStartOk(withBackend("warp"), "warp backend is valid");

  ExpectStartFails(withBitness(16), "bitness 16 is rejected");
  ExpectStartFails(withBitness(48), "bitness 48 is rejected");
  ExpectStartOk(withBitness(32), "bitness 32 is valid");
  ExpectStartOk(withBitness(64), "bitness 64 is valid");

  ExpectStartFails(withDpi(0), "dpi 0 is rejected");
  ExpectStartFails(withDpi(769), "dpi 769 is rejected");
  ExpectStartOk(withDpi(1), "dpi 1 is valid");
  ExpectStartOk(withDpi(768), "dpi 768 is valid");

  ExpectStartFails(withNoStages(), "no available stage is rejected");
  ExpectStartOk(withPid(0xFFFFFFFFu), "any uint32 pid is accepted");

  {
    fakeNow.store(0);
    perf::Probe probe(&FakeNow);
    perf::Options options = base;
    std::string error;
    Check(probe.Start(options, error), "start session for preservation");
    perf::Token first = probe.Begin(perf::Stage::Layout);
    fakeNow.store(1000000);
    probe.End(first);

    perf::Options invalid = options;
    invalid.backend = "invalid";
    Check(!probe.Start(invalid, error),
          "invalid Start during an active session fails");
    Check(probe.Enabled(), "invalid Start keeps the active session");
    Check(!probe.Start(options, error),
          "second Start during an active session fails");

    perf::Token second = probe.Begin(perf::Stage::Layout);
    fakeNow.store(2000000);
    probe.End(second);

    perf::Report report;
    Check(probe.Finish(report, error), "finish preserved session");
    Check(report.stages[StageIndex(perf::Stage::Layout)].samples == 2u,
          "failed Start did not discard samples");
  }
}

static void TestKnownDurations() {
  fakeNow.store(0);
  fakeClockCalls.store(0);

  perf::Probe probe(&FakeNow);
  perf::Options options = BaseOptions();
  std::string error;
  Check(probe.Start(options, error), "start known-duration session");

  for (int duration = 1; duration <= 5; ++duration) {
    const perf::Tick base = fakeNow.load(std::memory_order_relaxed);
    perf::Token token = probe.Begin(perf::Stage::Layout);
    Check(token.generation != 0u, "known-duration Begin returns a token");
    Check(token.stage == perf::Stage::Layout, "token records the stage");
    fakeNow.store(base + static_cast<perf::Tick>(duration) * 1000000);
    probe.End(token);
  }

  probe.Add(perf::Counter::FileMetadata, 2);
  probe.Add(perf::Counter::FileMetadata, 3);
  probe.Add(perf::Counter::Decode,
            (std::numeric_limits<std::uint64_t>::max)());
  probe.Add(perf::Counter::Decode, 1);

  perf::Report report;
  Check(probe.Finish(report, error), "finish known-duration session");

  const perf::StageReport& layout =
      report.stages[StageIndex(perf::Stage::Layout)];
  Check(layout.available, "layout is available");
  Check(layout.samples == 5u, "five layout samples");
  Check(layout.dropped == 0u, "no dropped layout samples");
  Check(layout.p50Ms == 3.0, "nearest-rank p50 is 3 ms");
  Check(layout.p95Ms == 5.0, "nearest-rank p95 is 5 ms");
  Check(layout.maxMs == 5.0, "max is 5 ms");

  const perf::StageReport& present =
      report.stages[StageIndex(perf::Stage::PresentComplete)];
  Check(!present.available, "present_complete is unavailable");
  Check(present.samples == 0u, "present_complete has no samples");
  Check(present.p50Ms == 0.0 && present.p95Ms == 0.0 &&
            present.maxMs == 0.0,
        "unavailable stage carries no fabricated timings");

  Check(report.counters[CounterIndex(perf::Counter::FileMetadata)] == 5u,
        "counter accumulates");
  Check(report.counters[CounterIndex(perf::Counter::Decode)] ==
            (std::numeric_limits<std::uint64_t>::max)(),
        "counter saturates instead of wrapping");
  Check(report.durationMs == 15.0, "durationMs follows the fake clock");
}

static void TestCapacityAndReuse() {
  fakeNow.store(1000);
  perf::Probe probe(&FakeNow);
  perf::Options options = BaseOptions();
  options.available[StageIndex(perf::Stage::PaintSubmit)] = true;
  std::string error;
  Check(probe.Start(options, error), "start capacity session");

  for (std::size_t index = 0; index < perf::kMaxSamples + 2; ++index) {
    perf::Token token = probe.Begin(perf::Stage::Layout);
    Check(token.generation != 0u, "capacity Begin returns a token");
    probe.End(token);
  }
  {
    perf::Token token = probe.Begin(perf::Stage::PaintSubmit);
    Check(token.generation != 0u, "second stage Begin returns a token");
    probe.End(token);
  }
  probe.Add(perf::Counter::FrameSubmit, 7);

  perf::Report report;
  Check(probe.Finish(report, error), "finish capacity session");
  const perf::StageReport& layout =
      report.stages[StageIndex(perf::Stage::Layout)];
  Check(layout.samples == perf::kMaxSamples,
        "sample storage keeps exactly 65536 entries");
  Check(layout.dropped == 2u, "two overflow samples are dropped");
  Check(report.stages[StageIndex(perf::Stage::PaintSubmit)].samples == 1u,
        "other stages keep recording after overflow");
  Check(report.counters[CounterIndex(perf::Counter::FrameSubmit)] == 7u,
        "first session counter value");

  fakeNow.store(2000);
  Check(probe.Start(options, error), "restart after Finish");
  perf::Report restarted;
  Check(probe.Finish(restarted, error), "finish restarted session");
  Check(restarted.counters[CounterIndex(perf::Counter::FrameSubmit)] == 0u,
        "counters do not leak across sessions");
  Check(restarted.stages[StageIndex(perf::Stage::Layout)].samples == 0u,
        "samples do not leak across sessions");
}

static void TestDeadlineAndClockRegression() {
  {
    fakeNow.store(0);
    perf::Probe probe(&FakeNow);
    perf::Options options = BaseOptions();
    options.durationSeconds = 1;
    std::string error;
    Check(probe.Start(options, error), "start deadline session");

    const perf::Tick deadline = 1000000000;
    fakeNow.store(deadline - 1);
    perf::Token token = probe.Begin(perf::Stage::Layout);
    Check(token.generation != 0u, "Begin one tick before deadline succeeds");

    fakeNow.store(deadline);
    probe.End(token);
    Check(!probe.Enabled(), "End exactly at deadline disables the probe");

    fakeNow.store(deadline + 1);
    perf::Token late = probe.Begin(perf::Stage::Layout);
    Check(late.generation == 0u, "Begin after deadline returns empty");
    probe.Add(perf::Counter::Decode, 5);

    perf::Report report;
    Check(probe.Finish(report, error), "finish deadline session");
    Check(report.stages[StageIndex(perf::Stage::Layout)].samples == 0u,
          "sample crossing the deadline is not recorded");
    Check(report.stages[StageIndex(perf::Stage::Layout)].dropped == 1u,
          "cross-deadline End is dropped");
    Check(report.counters[CounterIndex(perf::Counter::Decode)] == 0u,
          "Add after deadline does not count");
    Check(report.durationMs == 1000.0, "durationMs is capped at the deadline");
  }

  {
    fakeNow.store(0);
    perf::Probe probe(&FakeNow);
    perf::Options options = BaseOptions();
    options.durationSeconds = 1;
    std::string error;
    Check(probe.Start(options, error), "start passive-deadline session");
    fakeNow.store(1000000005);
    Check(probe.Enabled(),
          "deadline alone does not flip enabled without an operation");
    Check(!probe.Start(options, error),
          "Start is rejected until a deadline-expired session is finished");
    Check(probe.Enabled(),
          "rejected Start keeps the deadline-expired session pending");
    perf::Token token = probe.Begin(perf::Stage::Layout);
    Check(token.generation == 0u, "first operation after deadline returns empty");
    Check(!probe.Enabled(), "first operation after deadline disables sampling");
    perf::Report report;
    Check(probe.Finish(report, error), "finish passive-deadline session");
    Check(report.durationMs == 1000.0,
          "passive deadline still caps durationMs");
  }

  {
    fakeNow.store(1000);
    perf::Probe probe(&FakeNow);
    perf::Options options = BaseOptions();
    std::string error;
    Check(probe.Start(options, error), "start clock-regression session");
    perf::Token token = probe.Begin(perf::Stage::Layout);
    Check(token.generation != 0u, "Begin before clock regression");
    fakeNow.store(500);
    probe.End(token);
    Check(probe.Enabled(), "backward End keeps the session enabled");
    perf::Report report;
    Check(probe.Finish(report, error), "finish clock-regression session");
    Check(report.stages[StageIndex(perf::Stage::Layout)].samples == 0u,
          "backward End is not recorded");
    Check(report.stages[StageIndex(perf::Stage::Layout)].dropped == 1u,
          "backward End is dropped");
    Check(report.durationMs == 0.0, "backward Finish clamps duration to zero");
  }

  {
    fakeNow.store(1000);
    perf::Probe probe(&FakeNow);
    perf::Options options = BaseOptions();
    std::string error;
    Check(probe.Start(options, error), "start backward-Begin session");
    fakeNow.store(999);
    perf::Token token = probe.Begin(perf::Stage::Layout);
    Check(token.generation == 0u, "backward Begin returns empty");
    Check(!probe.Enabled(), "backward Begin disables sampling");
    perf::Report report;
    Check(probe.Finish(report, error), "finish backward-Begin session");
    Check(report.stages[StageIndex(perf::Stage::Layout)].samples == 0u,
          "backward Begin records nothing");
    Check(report.durationMs == 0.0, "backward Begin keeps duration at zero");
  }

  {
    fakeNow.store(-1);
    perf::Probe probe(&FakeNow);
    perf::Options options = BaseOptions();
    std::string error;
    Check(!probe.Start(options, error), "negative Start clock is rejected");
    Check(!error.empty(), "negative Start clock sets an error");
    Check(!probe.Enabled(), "negative Start clock leaves the probe disabled");
  }

  {
    fakeNow.store((std::numeric_limits<perf::Tick>::max)());
    perf::Probe probe(&FakeNow);
    perf::Options options = BaseOptions();
    options.durationSeconds = 1;
    std::string error;
    Check(!probe.Start(options, error),
          "Start clock near int64 max is rejected");
    Check(!error.empty(), "overflowing Start clock sets an error");
    Check(!probe.Enabled(), "overflowing Start clock leaves the probe disabled");
  }
}

static void TestOldTokensAndInvalidIndices() {
  fakeNow.store(0);
  perf::Probe probe(&FakeNow);
  perf::Options options = BaseOptions();
  std::string error;
  Check(probe.Start(options, error), "start old-token session");
  perf::Token old = probe.Begin(perf::Stage::Layout);
  Check(old.generation != 0u, "old token is valid");

  perf::Report first;
  Check(probe.Finish(first, error), "finish before old token ends");
  probe.End(old);
  Check(first.stages[StageIndex(perf::Stage::Layout)].samples == 0u,
        "token ended after Finish is ignored");

  fakeNow.store(1000);
  Check(probe.Start(options, error), "start new session");
  perf::Token current = probe.Begin(perf::Stage::Layout);
  Check(current.generation != 0u, "current token is valid");
  Check(current.generation != old.generation,
        "new session has a new generation");
  probe.End(old);
  fakeNow.store(2000);
  probe.End(current);

  perf::Token invalid = current;
  invalid.stage = perf::Stage::Count;
  probe.End(invalid);
  probe.End(perf::Token{});
  Check(probe.Begin(perf::Stage::Count).generation == 0u,
        "invalid Stage index returns empty");
  Check(probe.Begin(static_cast<perf::Stage>(999)).generation == 0u,
        "out-of-range Stage index returns empty");
  probe.Add(perf::Counter::Count, 1);
  probe.Add(static_cast<perf::Counter>(999), 1);

  perf::Report second;
  Check(probe.Finish(second, error), "finish new session");
  Check(second.stages[StageIndex(perf::Stage::Layout)].samples == 1u,
        "old token does not pollute the new session");
  Check(second.counters[CounterIndex(perf::Counter::FileMetadata)] == 0u,
        "invalid counters are ignored");
}

static void TestConcurrentProbe() {
  fakeNow.store(0);
  perf::Probe probe(&FakeNow);
  perf::Options options = BaseOptions();
  options.durationSeconds = 120;
  std::string error;
  Check(probe.Start(options, error), "start concurrent session");

  std::atomic<bool> stop{false};
  std::vector<std::thread> workers;
  workers.reserve(4);
  for (int worker = 0; worker < 4; ++worker) {
    workers.emplace_back([&probe, &stop]() {
      while (!stop.load(std::memory_order_relaxed)) {
        perf::Token token = probe.Begin(perf::Stage::Layout);
        probe.Add(perf::Counter::Decode, 1);
        probe.End(token);
      }
    });
  }

  perf::Report interrupted;
  std::string finishError;
  const bool finished = probe.Finish(interrupted, finishError);
  stop.store(true, std::memory_order_relaxed);
  for (auto& worker : workers) {
    worker.join();
  }
  Check(finished, "Finish during token traffic does not throw");
  Check(!probe.Enabled(), "Finish disables the probe");

  fakeNow.store(0);
  Check(probe.Start(options, error), "restart concurrent session");
  std::atomic<bool> go{false};
  std::vector<std::thread> measured;
  measured.reserve(4);
  for (int worker = 0; worker < 4; ++worker) {
    measured.emplace_back([&probe, &go]() {
      while (!go.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      for (int iteration = 0; iteration < 1000; ++iteration) {
        perf::Token token = probe.Begin(perf::Stage::Layout);
        probe.Add(perf::Counter::FileMetadata, 1);
        probe.End(token);
      }
    });
  }
  go.store(true, std::memory_order_release);
  for (auto& worker : measured) {
    worker.join();
  }

  perf::Report report;
  Check(probe.Finish(report, error), "finish concurrent measurement");
  Check(report.stages[StageIndex(perf::Stage::Layout)].samples == 4000u,
        "four threads record 4000 samples");
  Check(report.counters[CounterIndex(perf::Counter::FileMetadata)] == 4000u,
        "four threads record 4000 counter increments");
}

static void TestAllocationFailureRecovery() {
  fakeNow.store(0);
  perf::Probe probe(&FakeNow);
  perf::Options options = BaseOptions();
  options.sourceRevision = std::string(40, 'a');
  std::string error;
  Check(probe.Start(options, error), "start allocation-failure session");
  perf::Token token = probe.Begin(perf::Stage::Layout);
  fakeNow.store(1000000);
  probe.End(token);

  perf::Report output;
  output.durationMs = 123.0;
  output.stages[0].samples = 7;
  output.counters[0] = 9;

  failNextAllocation = true;
  const bool finished = probe.Finish(output, error);
  failNextAllocation = false;

  Check(!finished, "Finish reports allocation failure");
  Check(!error.empty(), "allocation failure sets an error");
  Check(!probe.Enabled(), "allocation failure disables the probe");
  Check(output.durationMs == 123.0, "failed Finish keeps durationMs");
  Check(output.stages[0].samples == 7u, "failed Finish keeps stage data");
  Check(output.counters[0] == 9u, "failed Finish keeps counters");

  fakeNow.store(2000000);
  Check(probe.Start(options, error), "restart after allocation failure");
  perf::Token recoveredToken = probe.Begin(perf::Stage::Layout);
  fakeNow.store(3000000);
  probe.End(recoveredToken);

  perf::Report recovered;
  Check(probe.Finish(recovered, error), "finish after allocation failure");
  Check(recovered.stages[StageIndex(perf::Stage::Layout)].samples == 1u,
        "restart records one sample");
  Check(recovered.stages[StageIndex(perf::Stage::Layout)].maxMs == 1.0,
        "restart sample duration is one millisecond");
  Check(recovered.stages[StageIndex(perf::Stage::Layout)].dropped == 0u,
        "restart has no dropped samples");
  for (std::size_t index = 0; index < perf::kCounterCount; ++index) {
    Check(recovered.counters[index] == 0u,
          "restart counters start at zero");
  }

  Check(!probe.Finish(output, error), "second Finish without a session fails");
  Check(output.durationMs == 123.0, "failed Finish keeps durationMs");
  Check(output.stages[0].samples == 7u, "failed Finish keeps stage data");
  Check(output.counters[0] == 9u, "failed Finish keeps counters");
}

class CommaNumpunct : public std::numpunct<char> {
 protected:
  char do_decimal_point() const override { return ','; }
  char do_thousands_sep() const override { return '.'; }
  std::string do_grouping() const override { return "\3"; }
};

static void TestWriteReport() {
  fakeNow.store(0);
  perf::Probe probe(&FakeNow);
  perf::Options options = BaseOptions();
  options.available[StageIndex(perf::Stage::CoreProcessKey)] = true;
  std::string error;
  Check(probe.Start(options, error), "start report session");
  perf::Token token = probe.Begin(perf::Stage::Layout);
  fakeNow.store(2000000);
  probe.End(token);
  probe.Add(perf::Counter::FileMetadata, 11);
  probe.Add(perf::Counter::DigestRead, 22);
  perf::Report report;
  Check(probe.Finish(report, error), "finish report session");
  report.durationMs = 1.5;
  report.stages[StageIndex(perf::Stage::Layout)].p50Ms = 2.5;

  std::ostringstream json;
  perf::WriteReport(json, report);
  const std::string text = json.str();

  Check(text.find("\"schemaVersion\":1") != std::string::npos,
        "schemaVersion is present");
  Check(text.find("\"sourceRevision\":\"unknown\"") != std::string::npos,
        "sourceRevision is present");
  Check(text.find("\"backend\":\"gdi\"") != std::string::npos,
        "backend is present");
  Check(text.find("\"scenarioId\":\"synthetic\"") != std::string::npos,
        "scenarioId is present");
  Check(text.find("\"requestedDurationSeconds\":30") != std::string::npos,
        "requested duration is present");

  const char* stageKeys[perf::kStageCount] = {
      "core_process_key", "context_response", "ipc_roundtrip", "layout",
      "resource_prepare", "paint_submit", "present_complete"};
  for (std::size_t index = 0; index < perf::kStageCount; ++index) {
    Check(text.find(std::string("\"") + stageKeys[index] + "\":") !=
              std::string::npos,
          "stage key is present");
  }

  const char* counterKeys[perf::kCounterCount] = {
      "file_metadata", "digest_read", "decode", "scale", "background_rebuild",
      "frame_submit"};
  for (std::size_t index = 0; index < perf::kCounterCount; ++index) {
    Check(text.find(std::string("\"") + counterKeys[index] + "\":") !=
              std::string::npos,
          "counter key is present");
  }

  Check(text.find("\"present_complete\":{\"status\":\"unavailable\","
                  "\"samples\":0,\"dropped\":0,"
                  "\"p50Ms\":null,\"p95Ms\":null,\"maxMs\":null}") !=
            std::string::npos,
        "unavailable stage timings are null");
  Check(text.find("\"core_process_key\":{\"status\":\"empty\","
                  "\"samples\":0,\"dropped\":0,"
                  "\"p50Ms\":0,\"p95Ms\":0,\"maxMs\":0}") !=
            std::string::npos,
        "empty stage timings are zero");
  Check(text.find("\"layout\":{\"status\":\"measured\"") != std::string::npos,
        "measured stage is marked measured");

  Check(text.find("filename") == std::string::npos,
        "no filename field is emitted");
  Check(text.find("input") == std::string::npos,
        "no input field is emitted");
  Check(text.find("candidate") == std::string::npos,
        "no candidate field is emitted");

  std::ostringstream localized;
  localized.imbue(std::locale(std::locale::classic(), new CommaNumpunct()));
  localized.precision(2);
  const std::locale originalLocale = localized.getloc();
  perf::WriteReport(localized, report);
  const std::string localizedText = localized.str();
  Check(localized.getloc() == originalLocale,
        "WriteReport leaves the caller locale unchanged");
  Check(localized.precision() == 2,
        "WriteReport leaves the caller precision unchanged");
  Check(localizedText.find("\"durationMs\":1.5") != std::string::npos,
        "decimal point survives a comma-decimal locale");
  Check(localizedText.find("\"p50Ms\":2.5") != std::string::npos,
        "stage decimal point survives a comma-decimal locale");
  Check(localizedText.find("1,5") == std::string::npos,
        "no comma decimal separator is emitted");
}

static void WriteSyntheticReport(const char* path) {
  fakeNow.store(0);
  perf::Probe probe(&FakeNow);
  perf::Options options = BaseOptions();
  options.sourceRevision = "0123456789abcdef0123456789abcdef01234567";
  options.scenarioId = "synthetic.file";
  options.backend = "dcomp";
  options.dpi = 144;
  options.available[StageIndex(perf::Stage::CoreProcessKey)] = true;
  options.available[StageIndex(perf::Stage::PaintSubmit)] = true;
  std::string error;
  Check(probe.Start(options, error), "start synthetic report session");

  for (int duration = 1; duration <= 3; ++duration) {
    const perf::Tick base = fakeNow.load(std::memory_order_relaxed);
    perf::Token token = probe.Begin(perf::Stage::Layout);
    fakeNow.store(base + static_cast<perf::Tick>(duration) * 1000000);
    probe.End(token);
  }
  probe.Add(perf::Counter::FileMetadata, 12);
  probe.Add(perf::Counter::Decode, 34);

  perf::Report report;
  Check(probe.Finish(report, error), "finish synthetic report session");

  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  Check(file.is_open(), "open synthetic report file");
  perf::WriteReport(file, report);
  file << '\n';
  file.close();
  Check(file.good(), "write synthetic report file");
}

int main(int argc, char** argv) {
  try {
    if (argc > 2) {
      std::cerr << "usage: native_performance_probe_test [report.json]\n";
      return 2;
    }

    TestClosedFastPath();
    TestValidation();
    TestKnownDurations();
    TestCapacityAndReuse();
    TestDeadlineAndClockRegression();
    TestOldTokensAndInvalidIndices();
    TestConcurrentProbe();
    TestAllocationFailureRecovery();
    TestWriteReport();

    if (argc == 2) {
      WriteSyntheticReport(argv[1]);
    }

    std::cout << "PASS native performance probe (C1a): 9 groups\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "FAIL unknown exception\n";
    return 1;
  }
}
