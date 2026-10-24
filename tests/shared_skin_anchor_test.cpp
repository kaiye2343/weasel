#include "../WeaselUI/SkinGeometry.h"
#include <cstddef>
#include <exception>
#include <iostream>

struct AnchorLayer {
  const char* id;
  bool right;
  bool bottom;
  int x;
  int y;
  int width;
  int height;
};

struct AnchorCase {
  const char* id;
  int dpi;
  weasel::SkinRect frame;
  weasel::SkinRect canvas;
  const AnchorLayer* layers;
  std::size_t layerCount;
};

#include "shared_skin_anchor_cases.inc"

void WriteRect(const weasel::SkinRect& rect) {
  std::cout << '[' << rect.left << ',' << rect.top << ',' << rect.right << ','
            << rect.bottom << ']';
}

int main(int argc, char**) try {
  if (argc != 1) {
    std::cerr << "Expected no arguments\n";
    return 2;
  }

  std::cout << "{\"schemaVersion\":1,\"scope\":\"decoration-anchors\","
               "\"coordinateSpace\":\"physical-pixels\",\"fixtureSha256\":\""
            << kFixtureSha256 << "\",\"cases\":[";

  bool firstCase = true;
  for (const auto& item : kCases) {
    if (!firstCase) std::cout << ',';
    firstCase = false;

    weasel::SkinRect bounds = item.canvas;
    std::vector<weasel::SkinRect> rectangles;
    rectangles.reserve(item.layerCount);
    for (std::size_t index = 0; index < item.layerCount; ++index) {
      const auto& layer = item.layers[index];
      auto rect = weasel::PlaceSkinImage(item.frame, layer.right, layer.bottom,
                                         layer.x, layer.y, layer.width,
                                         layer.height);
      rectangles.push_back(rect);
      bounds = weasel::ExpandSkinCanvas(bounds, rect);
    }

    std::cout << "{\"id\":\"" << item.id << "\",\"dpi\":" << item.dpi
              << ",\"frame\":";
    WriteRect(item.frame);
    std::cout << ",\"canvas\":";
    WriteRect(bounds);
    std::cout << ",\"origin\":[" << -bounds.left << ',' << -bounds.top
              << "],\"layers\":[";

    for (std::size_t index = 0; index < item.layerCount; ++index) {
      if (index > 0) std::cout << ',';
      const auto& layer = item.layers[index];
      const auto& rect = rectangles[index];
      std::cout << "{\"id\":\"" << layer.id << "\",\"rect\":";
      WriteRect(rect);
      std::cout << ",\"windowRect\":[" << rect.left - bounds.left << ','
                << rect.top - bounds.top << ',' << rect.right - bounds.left
                << ',' << rect.bottom - bounds.top << "]}";
    }

    std::cout << "]}";
  }

  std::cout << "]}\n";
  return 0;
} catch (const std::exception&) {
  std::cerr << "Native anchor fixture failed\n";
  return 1;
}
