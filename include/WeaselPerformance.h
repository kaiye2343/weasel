#pragma once

// Standard-library-only performance probe for Weasel native paths.
// This header does not open files, read the environment, install exit hooks,
// or start background threads. It only records explicitly instrumented work.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace weasel {
namespace performance {

enum class Stage : std::size_t {
  CoreProcessKey,
  ContextResponse,
  IpcRoundtrip,
  Layout,
  ResourcePrepare,
  PaintSubmit,
  PresentComplete,
  Count
};

enum class Counter : std::size_t {
  FileMetadata,
  DigestRead,
  Decode,
  Scale,
  BackgroundRebuild,
  FrameSubmit,
  Count
};

constexpr std::size_t kStageCount = static_cast<std::size_t>(Stage::Count);
constexpr std::size_t kCounterCount = static_cast<std::size_t>(Counter::Count);
constexpr std::size_t kMaxSamples = 65536;

using Tick = std::int64_t;
using ClockFunction = Tick (*)() noexcept;

// Monotonic nanoseconds from steady_clock. Never allocates or logs.
inline Tick MonotonicNow() noexcept {
  return static_cast<Tick>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

struct Options {
  std::string sourceRevision = "unknown";
  std::uint32_t pid = 0;
  std::uint32_t bitness = static_cast<std::uint32_t>(sizeof(void*) * 8);
  std::string backend = "gdi";
  std::uint32_t dpi = 96;
  std::string scenarioId = "synthetic";
  std::uint32_t durationSeconds = 30;
  std::array<bool, kStageCount> available{};
};

struct StageReport {
  bool available = false;
  std::uint64_t samples = 0;
  std::uint64_t dropped = 0;
  double p50Ms = 0.0;
  double p95Ms = 0.0;
  double maxMs = 0.0;
};

struct Report {
  Options options;
  double durationMs = 0.0;
  std::array<StageReport, kStageCount> stages{};
  std::array<std::uint64_t, kCounterCount> counters{};
};

struct Token {
  std::uint64_t generation = 0;
  Tick started = 0;
  Stage stage = Stage::Count;
};

class Probe {
 public:
  explicit Probe(ClockFunction clock = &MonotonicNow) noexcept
      : clock_(clock != nullptr ? clock : &MonotonicNow) {}

  bool Start(const Options& options, std::string& error) noexcept;
  Token Begin(Stage stage) noexcept;
  void End(Token token) noexcept;
  void Add(Counter counter, std::uint64_t amount = 1) noexcept;
  bool Finish(Report& output, std::string& error) noexcept;

  bool Enabled() const noexcept {
    return enabled_.load(std::memory_order_relaxed);
  }

 private:
  static void SetError(std::string& error, const char* message) noexcept {
    try {
      error.assign(message);
    } catch (...) {
    }
  }

  static bool IsHexDigit(char value) noexcept {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
  }

  static bool ValidateOptions(const Options& options,
                              std::string& error) noexcept;

  std::atomic<bool> enabled_{false};
  std::mutex mutex_;
  ClockFunction clock_;
  bool hasSession_ = false;
  std::uint64_t generation_ = 0;
  Options options_;
  Tick started_ = 0;
  Tick deadline_ = 0;
  std::array<std::vector<Tick>, kStageCount> samples_;
  std::array<std::uint64_t, kStageCount> dropped_{};
  std::array<std::uint64_t, kCounterCount> counters_{};
};

inline bool Probe::ValidateOptions(const Options& options,
                                   std::string& error) noexcept {
  const std::string& revision = options.sourceRevision;
  bool revisionOk = (revision == "unknown");
  if (!revisionOk && revision.size() == 40) {
    revisionOk = true;
    for (char value : revision) {
      if (!IsHexDigit(value)) {
        revisionOk = false;
        break;
      }
    }
  }
  if (!revisionOk) {
    SetError(error,
             "sourceRevision must be \"unknown\" or exactly 40 hex characters");
    return false;
  }

  if (options.scenarioId.empty() || options.scenarioId.size() > 64) {
    SetError(error, "scenarioId must contain between 1 and 64 characters");
    return false;
  }
  for (char value : options.scenarioId) {
    const bool allowed =
        (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
        (value >= '0' && value <= '9') || value == '.' || value == '_' ||
        value == '-';
    if (!allowed) {
      SetError(error,
               "scenarioId may only contain ASCII letters, digits, '.', '_' or "
               "'-'");
      return false;
    }
  }

  if (options.backend != "gdi" && options.backend != "dcomp" &&
      options.backend != "warp") {
    SetError(error, "backend must be one of \"gdi\", \"dcomp\" or \"warp\"");
    return false;
  }

  if (options.bitness != 32 && options.bitness != 64) {
    SetError(error, "bitness must be either 32 or 64");
    return false;
  }

  if (options.dpi < 1 || options.dpi > 768) {
    SetError(error, "dpi must be between 1 and 768");
    return false;
  }

  if (options.durationSeconds < 1 || options.durationSeconds > 120) {
    SetError(error, "durationSeconds must be between 1 and 120");
    return false;
  }

  bool anyAvailable = false;
  for (bool available : options.available) {
    anyAvailable = anyAvailable || available;
  }
  if (!anyAvailable) {
    SetError(error, "at least one stage must be marked available");
    return false;
  }

  return true;
}

inline bool Probe::Start(const Options& options, std::string& error) noexcept {
  try {
    if (!ValidateOptions(options, error)) {
      return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (hasSession_) {
      SetError(error, "a probe session is already active");
      return false;
    }
    if (generation_ == (std::numeric_limits<std::uint64_t>::max)()) {
      SetError(error, "probe generation counter is exhausted");
      return false;
    }

    const Tick now = clock_();
    const std::int64_t durationNs =
        static_cast<std::int64_t>(options.durationSeconds) * 1000000000LL;
    if (now < 0 ||
        now > (std::numeric_limits<Tick>::max)() - durationNs) {
      SetError(error, "probe clock is outside the supported range");
      return false;
    }

    std::array<std::vector<Tick>, kStageCount> nextSamples;
    for (std::size_t index = 0; index < kStageCount; ++index) {
      if (options.available[index]) {
        nextSamples[index].reserve(kMaxSamples);
      }
    }

    Options nextOptions(options);
    options_ = std::move(nextOptions);
    samples_ = std::move(nextSamples);
    dropped_.fill(0);
    counters_.fill(0);
    started_ = now;
    deadline_ = now + durationNs;
    ++generation_;
    hasSession_ = true;
    enabled_.store(true, std::memory_order_relaxed);
    error.clear();
    return true;
  } catch (...) {
    SetError(error, "failed to start probe session");
    return false;
  }
}

inline Token Probe::Begin(Stage stage) noexcept {
  if (!enabled_.load(std::memory_order_relaxed)) {
    return Token{};
  }
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_.load(std::memory_order_relaxed) || !hasSession_) {
      return Token{};
    }
    const std::size_t index = static_cast<std::size_t>(stage);
    if (index >= kStageCount || !options_.available[index]) {
      return Token{};
    }
    const Tick now = clock_();
    if (now >= deadline_) {
      enabled_.store(false, std::memory_order_relaxed);
      return Token{};
    }
    if (now < started_) {
      enabled_.store(false, std::memory_order_relaxed);
      return Token{};
    }
    return Token{generation_, now, stage};
  } catch (...) {
    enabled_.store(false, std::memory_order_relaxed);
    return Token{};
  }
}

inline void Probe::End(Token token) noexcept {
  if (token.generation == 0) {
    return;
  }
  if (!enabled_.load(std::memory_order_relaxed)) {
    return;
  }
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_.load(std::memory_order_relaxed) || !hasSession_) {
      return;
    }
    const std::size_t index = static_cast<std::size_t>(token.stage);
    if (index >= kStageCount || !options_.available[index]) {
      return;
    }
    if (token.generation != generation_) {
      return;
    }
    const Tick now = clock_();
    if (now < token.started) {
      ++dropped_[index];
      return;
    }
    if (now >= deadline_) {
      ++dropped_[index];
      enabled_.store(false, std::memory_order_relaxed);
      return;
    }
    std::vector<Tick>& samples = samples_[index];
    if (samples.size() < kMaxSamples) {
      samples.push_back(now - token.started);
    } else {
      ++dropped_[index];
    }
  } catch (...) {
    enabled_.store(false, std::memory_order_relaxed);
  }
}

inline void Probe::Add(Counter counter, std::uint64_t amount) noexcept {
  if (!enabled_.load(std::memory_order_relaxed)) {
    return;
  }
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_.load(std::memory_order_relaxed) || !hasSession_) {
      return;
    }
    const std::size_t index = static_cast<std::size_t>(counter);
    if (index >= kCounterCount) {
      return;
    }
    const Tick now = clock_();
    if (now >= deadline_ || now < started_) {
      enabled_.store(false, std::memory_order_relaxed);
      return;
    }
    std::uint64_t& value = counters_[index];
    const std::uint64_t maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (amount > maximum - value) {
      value = maximum;
    } else {
      value += amount;
    }
  } catch (...) {
    enabled_.store(false, std::memory_order_relaxed);
  }
}

inline bool Probe::Finish(Report& output, std::string& error) noexcept {
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasSession_) {
      SetError(error, "no active probe session");
      return false;
    }

    try {
      const Tick now = clock_();
      enabled_.store(false, std::memory_order_relaxed);

      Report report;
      report.options = options_;
      const Tick capped = now < deadline_ ? now : deadline_;
      report.durationMs =
          capped <= started_
              ? 0.0
              : static_cast<double>(capped - started_) / 1000000.0;

      for (std::size_t index = 0; index < kStageCount; ++index) {
        StageReport& stage = report.stages[index];
        stage.available = options_.available[index];
        stage.dropped = dropped_[index];

        std::vector<Tick> values = samples_[index];
        stage.samples = static_cast<std::uint64_t>(values.size());
        if (!values.empty()) {
          std::sort(values.begin(), values.end());
          const std::uint64_t count =
              static_cast<std::uint64_t>(values.size());
          const std::uint64_t rank50 = (count * 50 + 99) / 100 - 1;
          const std::uint64_t rank95 = (count * 95 + 99) / 100 - 1;
          stage.p50Ms =
              static_cast<double>(values[static_cast<std::size_t>(rank50)]) /
              1000000.0;
          stage.p95Ms =
              static_cast<double>(values[static_cast<std::size_t>(rank95)]) /
              1000000.0;
          stage.maxMs = static_cast<double>(values.back()) / 1000000.0;
        }
      }
      report.counters = counters_;

      for (auto& values : samples_) {
        std::vector<Tick>().swap(values);
      }
      dropped_.fill(0);
      counters_.fill(0);
      started_ = 0;
      deadline_ = 0;
      hasSession_ = false;

      output = std::move(report);
      error.clear();
      return true;
    } catch (...) {
      enabled_.store(false, std::memory_order_relaxed);
      hasSession_ = false;
      for (auto& values : samples_) {
        std::vector<Tick>().swap(values);
      }
      dropped_.fill(0);
      counters_.fill(0);
      started_ = 0;
      deadline_ = 0;
      SetError(error, "failed to finish probe session");
      return false;
    }
  } catch (...) {
    SetError(error, "failed to lock probe session");
    return false;
  }
}

class Scope {
 public:
  Scope(Probe& probe, Stage stage) noexcept
      : probe_(probe), token_(probe.Begin(stage)) {}

  ~Scope() noexcept { probe_.End(token_); }

  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

 private:
  Probe& probe_;
  Token token_;
};

// Explicit final serialization only. No filename or path arguments.
inline void WriteReport(std::ostream& stream, const Report& report) {
  static const char* const kStageKeys[kStageCount] = {
      "core_process_key", "context_response", "ipc_roundtrip", "layout",
      "resource_prepare", "paint_submit", "present_complete"};
  static const char* const kCounterKeys[kCounterCount] = {
      "file_metadata", "digest_read", "decode", "scale", "background_rebuild",
      "frame_submit"};

  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::setprecision(std::numeric_limits<double>::max_digits10);

  out << '{';
  out << "\"schemaVersion\":1";
  out << ",\"sourceRevision\":" << std::quoted(report.options.sourceRevision);
  out << ",\"pid\":" << report.options.pid;
  out << ",\"bitness\":" << report.options.bitness;
  out << ",\"backend\":" << std::quoted(report.options.backend);
  out << ",\"dpi\":" << report.options.dpi;
  out << ",\"scenarioId\":" << std::quoted(report.options.scenarioId);
  out << ",\"requestedDurationSeconds\":"
      << report.options.durationSeconds;
  out << ",\"durationMs\":" << report.durationMs;

  out << ",\"stages\":{";
  for (std::size_t index = 0; index < kStageCount; ++index) {
    if (index != 0) {
      out << ',';
    }
    const StageReport& stage = report.stages[index];
    const char* status = !stage.available
                             ? "unavailable"
                             : (stage.samples == 0 ? "empty" : "measured");
    out << std::quoted(kStageKeys[index]) << ':' << '{';
    out << "\"status\":" << std::quoted(status);
    out << ",\"samples\":" << stage.samples;
    out << ",\"dropped\":" << stage.dropped;
    if (!stage.available) {
      out << ",\"p50Ms\":null,\"p95Ms\":null,\"maxMs\":null";
    } else {
      out << ",\"p50Ms\":" << stage.p50Ms;
      out << ",\"p95Ms\":" << stage.p95Ms;
      out << ",\"maxMs\":" << stage.maxMs;
    }
    out << '}';
  }
  out << '}';

  out << ",\"counters\":{";
  for (std::size_t index = 0; index < kCounterCount; ++index) {
    if (index != 0) {
      out << ',';
    }
    out << std::quoted(kCounterKeys[index]) << ':' << report.counters[index];
  }
  out << "}}";

  stream << out.str();
}

// Construct on first use and share across translation units of this PE.
// This is not a cross-process singleton.
inline Probe& GetProcessProbe() noexcept {
  // Several native endpoints use /Zc:threadSafeInit-. These two objects are
  // constant-initialized; call_once protects the dynamic Probe construction.
  static std::atomic<Probe*> cached{nullptr};
  if (Probe* probe = cached.load(std::memory_order_acquire)) return *probe;
  static std::once_flag once;
  std::call_once(once, [] {
    static Probe probe;
    cached.store(&probe, std::memory_order_release);
  });
  return *cached.load(std::memory_order_acquire);
}

}  // namespace performance
}  // namespace weasel
