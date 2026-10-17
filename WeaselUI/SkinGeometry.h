#pragma once
#include <algorithm>

namespace weasel {
struct SkinRect {
  int left, top, right, bottom;
};

// Offsets belong to visible frame corners, never the outer shadow canvas.
inline SkinRect PlaceSkinImage(SkinRect frame,
                               bool right,
                               bool bottom,
                               int x,
                               int y,
                               int width,
                               int height) {
  const int left = (right ? frame.right : frame.left) + x;
  const int top = (bottom ? frame.bottom : frame.top) + y;
  return {left, top, left + width, top + height};
}
inline SkinRect PlaceSkinImage(int width, int height, bool right, bool bottom,
                               int x, int y, int image_width, int image_height) {
  return PlaceSkinImage({0, 0, width, height}, right, bottom, x, y, image_width, image_height);
}

inline SkinRect ExpandSkinCanvas(SkinRect bounds, SkinRect image) {
  return {(std::min)(bounds.left, image.left),
          (std::min)(bounds.top, image.top),
          (std::max)(bounds.right, image.right),
          (std::max)(bounds.bottom, image.bottom)};
}
}  // namespace weasel
