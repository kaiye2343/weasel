#include "../WeaselUI/SkinGeometry.h"
#include <chrono>
#include <cstdint>
#include <iostream>

// Pure geometry only: no HWND, file IO, image decoding or Rime session.
// Informational distributions, not a machine-dependent performance gate.
int main() {
  using namespace weasel;
  volatile std::uint64_t checksum = 0;
  std::cout << "dpi,layers,mode,p50_ns,p95_ns,scope\n";
  for (int dpi : {96, 144, 192}) for (int count : {0, 2, 8}) {
    auto q = [dpi](int n) { return n * dpi / 96; };
    std::vector<SkinRect> images;
    for (int i = 0; i < count; ++i)
      images.push_back(i % 2 ? SkinRect{q(480), 0, q(650), q(170)} : SkinRect{0, 0, q(80), q(100)});
    for (int preference : {-1, -2, 0}) {
      std::vector<double> batches;
      for (int batch = -1; batch < 50; ++batch) {
        std::uint64_t total = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int key = 0; key < 2000; ++key) {
          const int effective = ResolveCandidatePlacement(preference, !images.empty());
          const int shift = key % 37;
          CandidateWindowPosition result{q(200 + shift), q(324), false, false};
          if (effective != -1) result = PlaceCandidateWindow(
              {q(200 + shift), q(300), q(350 + shift), q(324)}, {0, 0, q(1200), q(900)},
              q(650), q(170), {q(30), q(80), q(530), q(130)}, images, 0, q(6), effective, true);
          total += static_cast<std::uint64_t>(result.x + result.y);
        }
        const auto stop = std::chrono::steady_clock::now();
        checksum = checksum + total;
        if (batch >= 0) batches.push_back(std::chrono::duration<double, std::nano>(stop - start).count() / 2000.0);
      }
      std::sort(batches.begin(), batches.end());
      const char* mode = preference == -1 ? "native-selector" : preference == -2 ? "appearance-default" : "explicit-auto";
      std::cout << dpi << ',' << count << ',' << mode << ',' << batches[24] << ',' << batches[47]
                << ",pure-placement-no-window-submit\n";
    }
  }
  return checksum ? 0 : 1;
}
