// No windows or real data. Check first-use construction of the actual endpoint.
#include <windows.h>
#include <WeaselPerformance.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace perf = weasel::performance;
perf::Probe* ProcessProbeFromPeer() noexcept;
void RecordProcessProbeFromPeer() noexcept;

static void Check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

int main() try {
  std::array<perf::Probe*, 8> seen{};
  std::array<std::thread, 8> workers;
  std::atomic<std::size_t> ready{0};
  std::atomic<bool> go{false};
  for (std::size_t index = 0; index < workers.size(); ++index)
    workers[index] = std::thread([&, index] {
      ready.fetch_add(1, std::memory_order_release);
      while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
      seen[index] = &perf::GetProcessProbe();
    });
  while (ready.load(std::memory_order_acquire) != workers.size()) std::this_thread::yield();
  go.store(true, std::memory_order_release);
  for (auto& worker : workers) worker.join();
  auto& probe = *seen[0];
  Check(!probe.Enabled(), "process probe defaults disabled");
  for (const auto* pointer : seen)
    Check(pointer == seen[0], "concurrent first use returns one probe");
  Check(ProcessProbeFromPeer() == seen[0], "translation units share one process probe");
  perf::Options options;
  options.scenarioId = "synthetic.process-lifetime";
  options.available[static_cast<std::size_t>(perf::Stage::ContextResponse)] = true;
  std::string error;
  Check(probe.Start(options, error), "process probe starts with initialized clock");
  { perf::Scope scope(probe, perf::Stage::ContextResponse); }
  RecordProcessProbeFromPeer();
  perf::Report report;
  Check(probe.Finish(report, error), "process probe finishes");
  Check(report.stages[static_cast<std::size_t>(perf::Stage::ContextResponse)].samples == 2,
        "both translation units record into same probe");
  Check(!probe.Enabled(), "process probe stops after report");
  std::cout << "PASS process probe lifetime: concurrent first use, real clock, shared translation units\n";
  return 0;
} catch (const std::exception& error) {
  std::cerr << "FAIL " << error.what() << '\n';
  return 1;
}
