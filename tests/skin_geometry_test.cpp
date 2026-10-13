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
}
