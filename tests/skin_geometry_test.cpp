#include "../WeaselUI/SkinGeometry.h"
#include <cassert>

int main() {
  using namespace weasel;
  SkinRect base{0, 0, 500, 70};
  const auto wing = PlaceSkinImage(500, 70, false, false, -12, -40, 64, 64);
  const auto character =
      PlaceSkinImage(500, 70, true, true, -12, -140, 168, 168);
  auto canvas = ExpandSkinCanvas(ExpandSkinCanvas(base, wing), character);
  assert(canvas.left == -12 && canvas.top == -70);
  assert(canvas.right == 656 && canvas.bottom == 98);
  const auto grown = PlaceSkinImage(900, 70, true, true, -12, -140, 168, 168);
  assert(grown.left - character.left == 400);
  assert(grown.top == character.top);
  const auto scaled =
      PlaceSkinImage(1000, 140, true, true, -24, -280, 336, 336);
  assert(scaled.left == character.left * 2);
  assert(scaled.bottom == character.bottom * 2);
  const auto inner = ExpandSkinCanvas(base, {10, 10, 30, 30});
  assert(inner.left == 0 && inner.top == 0 && inner.right == 500);
  // A visible frame has an origin inside the shadow canvas. Decorations must
  // use the same four frame corners as the offline designer.
  SkinRect frame{16, 20, 516, 90};
  const auto framed = PlaceSkinImage(frame, true, true, -176, -149, 221, 184);
  assert(framed.left - frame.left == 500 - 176);
  assert(framed.top - frame.top == 70 - 149);
  assert(framed.right - frame.right == 45);
  const auto leftWing = PlaceSkinImage(frame, false, false, -29, -52, 75, 62);
  assert(leftWing.left - frame.left == -29 && leftWing.top - frame.top == -52);
  // Changing shadow padding must not change relative image placement.
  SkinRect other{32, 40, 532, 110};
  const auto shifted = PlaceSkinImage(other, true, true, -176, -149, 221, 184);
  assert(shifted.left - other.left == framed.left - frame.left);
  assert(shifted.bottom - other.bottom == framed.bottom - frame.bottom);
}
