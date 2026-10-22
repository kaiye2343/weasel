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
  // Reproduce the old placement bug: aligning the frame to the caret bottom
  // puts the top decoration across the composing line.
  SkinRect caret{200, 300, 350, 324}, work{0, 0, 1200, 900};
  assert(ShouldUpdateCandidateAnchor(caret, caret, false));
  assert(!ShouldUpdateCandidateAnchor(caret, {200, 302, 350, 326}, true));
  assert(ShouldUpdateCandidateAnchor(caret, {200, 320, 350, 344}, true));
  assert(ShouldUpdateCandidateAnchor(caret, {202, 300, 352, 324}, true));
  SkinRect textFrame{30, 80, 530, 130};
  std::vector<SkinRect> images{{0, 0, 80, 100}, {480, 0, 650, 170}};
  const int oldY = caret.bottom + 6 - textFrame.top;
  assert(oldY < caret.bottom && oldY + images[0].bottom > caret.top);
  auto placed = PlaceCandidateWindow(caret, work, 650, 170, textFrame, images, 0, 6, 0, true);
  assert(placed.y == 330 && !placed.above && !placed.constrained);
  assert(placed.x + textFrame.left == caret.left);
  auto withoutImages = PlaceCandidateWindow(caret, work, 650, 170, textFrame, images, 0, 6, 0, false);
  assert(withoutImages.y == oldY);
  // A character outside the input line's horizontal range adds no extra gap.
  std::vector<SkinRect> rightOnly{images[1]};
  auto right = PlaceCandidateWindow(caret, work, 650, 170, textFrame, rightOnly, 0, 6, 0, true);
  assert(right.y == oldY);
  auto above = PlaceCandidateWindow(caret, work, 650, 170, textFrame, images, 0, 6, 2, true);
  assert(above.above && above.y + images[0].bottom <= caret.top - 6);
  // At a right edge the character moves over the input, so it must be avoided.
  SkinRect edgeCaret{1120, 300, 1180, 324};
  auto edge = PlaceCandidateWindow(edgeCaret, work, 650, 170, textFrame, rightOnly, 0, 6, 0, true);
  assert(edge.x == 550 && edge.y == 330);
  auto bottomEdge = PlaceCandidateWindow({200, 850, 350, 874}, work, 650, 170, textFrame, images, 0, 6, 1, true);
  assert(bottomEdge.above && bottomEdge.y + textFrame.bottom <= 850 - 6);
  auto topEdge = PlaceCandidateWindow({200, 10, 350, 34}, work, 650, 170, textFrame, images, 0, 6, 2, true);
  assert(!topEdge.above && topEdge.y >= 40);
  auto huge = PlaceCandidateWindow(caret, {0, 0, 400, 100}, 650, 170, textFrame, images, 256, 6, 0, true);
  assert(huge.x == 0 && huge.y == 0 && huge.constrained);
  auto negative = PlaceCandidateWindow({-900, 300, -750, 324}, {-1200, 0, 0, 900}, 650, 170, textFrame, images, -40, 6, 0, true);
  assert(negative.x == -970 && negative.y == 330);
  // Same physical results at 100%, 150%, 200%; no input rectangle mutation.
  for (int percent : {100, 150, 200}) {
    auto scale = [percent](int value) { return value * percent / 100; };
    auto rect = [&](SkinRect r) { return SkinRect{scale(r.left), scale(r.top), scale(r.right), scale(r.bottom)}; };
    std::vector<SkinRect> scaledImages; for (auto r : images) scaledImages.push_back(rect(r));
    auto atDpi = PlaceCandidateWindow(rect(caret), rect(work), scale(650), scale(170), rect(textFrame), scaledImages, scale(12), scale(6), 0, true);
    assert(atDpi.x == scale(182) && atDpi.y == scale(330));
    for (int repeat = 0; repeat < 20; ++repeat) {
      auto next = PlaceCandidateWindow(rect(caret), rect(work), scale(650), scale(170), rect(textFrame), scaledImages, scale(12), scale(6), 0, true);
      assert(next.x == atDpi.x && next.y == atDpi.y);
    }
  }
}
