#pragma once
#include <algorithm>
#include <vector>

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

struct CandidateWindowPosition {
  int x, y;
  bool above, constrained;
};
inline bool ShouldUpdateCandidateAnchor(SkinRect previous, SkinRect next, bool hasPrevious) {
  // Preserve the accepted source anchor across layout refreshes, not just the
  // MoveTo call that received a host's 1-2 pixel vertical jitter.
  return !hasPrevious || previous.left != next.left || previous.right != next.right ||
      next.top - previous.top <= -6 || next.top - previous.top >= 6 ||
      next.bottom - previous.bottom <= -6 || next.bottom - previous.bottom >= 6;
}

// All inputs are physical pixels. frame/images are relative to the window;
// caret/work are screen coordinates. Never write adjusted coordinates to caret.
inline CandidateWindowPosition PlaceCandidateWindow(
    SkinRect caret, SkinRect work, int width, int height, SkinRect frame,
    const std::vector<SkinRect>& images, int offsetX, int gap,
    int preference, bool avoidDecorations) {
  auto clamp = [](int value, int low, int high) {
    return (std::max)(low, (std::min)(value, (std::max)(low, high)));
  };
  const int x = clamp(caret.left + offsetX - frame.left,
                      work.left, work.right - width);
  int top = frame.top, bottom = frame.bottom;
  if (avoidDecorations) {
    for (const auto& image : images) {
      // A right-side character need not move the text down unless it actually
      // intersects the composing text horizontally after screen-edge clamping.
      if (x + image.left < (std::max)(caret.right, caret.left + 1) &&
          x + image.right > caret.left) {
        top = (std::min)(top, image.top);
        bottom = (std::max)(bottom, image.bottom);
      }
    }
  }
  const int below = caret.bottom + gap - top;
  const int above = caret.top - gap - bottom;
  const bool fitsBelow = below >= work.top && below + height <= work.bottom;
  const bool fitsAbove = above >= work.top && above + height <= work.bottom;
  bool useAbove = preference == 2;
  if (useAbove ? !fitsAbove : !fitsBelow) {
    if (useAbove ? fitsBelow : fitsAbove) useAbove = !useAbove;
    else useAbove = caret.top - work.top > work.bottom - caret.bottom;
  }
  const int wantedY = useAbove ? above : below;
  const int y = clamp(wantedY, work.top, work.bottom - height);
  return {x, y, useAbove, y != wantedY || width > work.right - work.left || height > work.bottom - work.top};
}
}  // namespace weasel
