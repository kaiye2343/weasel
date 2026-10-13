#pragma once
#include <algorithm>

namespace weasel {
struct SkinRect {
  int left, top, right, bottom;
};

// Offsets are relative to an anchor on the unadorned candidate canvas.
inline SkinRect PlaceSkinImage(int canvas_width,
                               int canvas_height,
                               bool right,
                               bool bottom,
                               int x,
                               int y,
                               int width,
                               int height) {
  const int left = (right ? canvas_width : 0) + x;
  const int top = (bottom ? canvas_height : 0) + y;
  return {left, top, left + width, top + height};
}

inline SkinRect ExpandSkinCanvas(SkinRect bounds, SkinRect image) {
  return {(std::min)(bounds.left, image.left),
          (std::min)(bounds.top, image.top),
          (std::max)(bounds.right, image.right),
          (std::max)(bounds.bottom, image.bottom)};
}
}  // namespace weasel
