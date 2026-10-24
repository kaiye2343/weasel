// Run the real OnMouseMove method without a HWND or simulated mouse input.
#include <windows.h>
#include <windowsx.h>
#include <atlbase.h>
#include <atltypes.h>
#include <WeaselIPCData.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace ATL;
using namespace weasel;
constexpr size_t MAX_CANDIDATES_COUNT = 10;
struct LayoutFixture {
  std::vector<CRect> rectangles{CRect(0, 0, 100, 30), CRect(100, 0, 200, 30)};
  CRect GetCandidateRect(int i) {
    return rectangles.at(static_cast<size_t>(i));
  }
};
class WeaselPanel {
 public:
  LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
  void ClientToScreen(POINT* p) { p->x += screen.x; p->y += screen.y; }
  void InvalidateRect(const RECT*, bool) { ++invalidations; }
  int DPI_SCALE(int n) { return static_cast<int>(n * scale); }
  HWND m_hWnd = nullptr;
  float scale = 1.0f;
  UIStyle m_style;
  Context m_ctx;
  bool m_mouse_entry = true, m_istorepos = false, m_hasMousePos = false;
  CPoint screen = CPoint(10, 20), m_skin_origin = CPoint(20, 10);
  CPoint m_lastMousePos = {-1, -1};
  size_t m_candidateCount = 2;
  int m_offsetys[10]{}, m_hoverIndex = -1, calls = 0, invalidations = 0;
  CRect rcw;
  LayoutFixture fixture;
  LayoutFixture* m_layout = &fixture;
  std::function<void(size_t*, size_t*, bool*, bool*)> _UICallback =
      [this](size_t*, size_t* index, bool*, bool*) { ++calls; m_ctx.cinfo.highlighted = int(*index); };
  void Move(int x, int y) {
    BOOL handled = FALSE; OnMouseMove(WM_MOUSEMOVE, 0, MAKELPARAM(x, y), handled);
  }
};
#include "hover_under_test.inc"
static void Check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
void CheckHoverEdges() {
  for (const int dpi : {96, 144, 192}) {
    const float s = static_cast<float>(dpi) / 96.0f;
    const auto q = [s](int value) { return static_cast<int>(value * s); };
    const int left = -q(4), top = q(7) - q(3), right = q(100) + q(4),
              bottom = q(30) + q(7) + q(3);
    struct EdgePoint { int x; int y; bool inside; };
    const EdgePoint points[] = {
        {left, top, true},           {right - 1, top, true},
        {left, bottom - 1, true},    {right - 1, bottom - 1, true},
        {left - 1, top, false},      {right, top, false},
        {left, top - 1, false},      {left, bottom, false},
        {right, bottom, false},      {left + 1, top + 1, true},
        {right + 1, top + 1, false}, {left + 1, bottom + 1, false},
        {0, q(15), true}};
    for (const EdgePoint& point : points) {
      WeaselPanel p;
      p.scale = s;
      p.fixture.rectangles = {CRect(q(300), 0, q(400), q(30)),
                              CRect(0, 0, q(100), q(30))};
      p.m_style.hover_type = UIStyle::HILITE;
      p.m_style.hilite_padding_x = 4;
      p.m_style.hilite_padding_y = 3;
      p.m_ctx.cinfo.highlighted = 0;
      p.m_skin_origin = CPoint(q(20), q(10));
      p.screen = CPoint(-q(600), q(40));
      p.m_istorepos = true;
      p.m_offsetys[1] = q(7);
      p.Move(-1000, -1000);
      p.Move(point.x + p.m_skin_origin.x, point.y + p.m_skin_origin.y);
      Check(p.calls == (point.inside ? 1 : 0),
            "DPI/padding/origin/reorder half-open hover edges");
      Check(p.m_ctx.cinfo.highlighted == (point.inside ? 1 : 0),
            "edge target matches real hover callback");
    }
  }
}
void CheckHoverOverlap() {
  WeaselPanel hilite;
  hilite.m_style.hover_type = UIStyle::HILITE;
  hilite.m_style.hilite_padding_x = 4;
  hilite.m_style.hilite_padding_y = 0;
  hilite.m_ctx.cinfo.highlighted = 1;
  hilite.Move(-1000, -1000);
  hilite.Move(119, 20);
  Check(hilite.calls == 2 && hilite.m_ctx.cinfo.highlighted == 1,
        "HILITE overlap follows production callback loop order");
  WeaselPanel semi;
  semi.m_style.hover_type = UIStyle::SEMI_HILITE;
  semi.m_style.hilite_padding_x = 4;
  semi.m_style.hilite_padding_y = 0;
  semi.m_ctx.cinfo.highlighted = 0;
  semi.Move(-1000, -1000);
  semi.Move(119, 20);
  Check(semi.calls == 0 && semi.m_hoverIndex == 1 && semi.invalidations == 1,
        "SEMI_HILITE overlap preserves non-highlighted hover result");
  WeaselPanel reset;
  reset.m_style.hover_type = UIStyle::SEMI_HILITE;
  reset.m_style.hilite_padding_x = 4;
  reset.m_style.hilite_padding_y = 0;
  reset.m_ctx.cinfo.highlighted = 1;
  reset.Move(-1000, -1000);
  reset.Move(119, 20);
  Check(reset.calls == 0 && reset.m_hoverIndex == -1 && reset.invalidations == 2,
        "highlighted later candidate clears prior semi hover");
  WeaselPanel empty;
  empty.m_style.hover_type = UIStyle::HILITE;
  empty.m_candidateCount = 0;
  empty.Move(-1000, -1000);
  empty.Move(30, 20);
  Check(empty.calls == 0, "empty hover page has no callback");
}
int main() try {
  WeaselPanel p; p.m_style.hover_type = UIStyle::HILITE;
  p.m_style.hilite_padding_x = p.m_style.hilite_padding_y = 0;
  p.m_ctx.cinfo.highlighted = 1;
  p.Move(30, 20); Check(p.calls == 0, "first notification must not select");
  p.Move(30, 20); Check(p.calls == 0, "stationary cursor must not select");
  p.screen.x = 5; p.m_skin_origin.x = 25;
  p.Move(35, 20); Check(p.calls == 0, "window/decoration shift with stationary screen cursor");
  p.Move(36, 20); Check(p.calls == 1 && p.m_ctx.cinfo.highlighted == 0, "moving cursor hit");
  p.m_ctx.cinfo.highlighted = 1;
  p.Move(5, 20); Check(p.calls == 1, "decoration must not select candidate");
  WeaselPanel negative; negative.m_style.hover_type = UIStyle::HILITE;
  negative.m_style.hilite_padding_x = negative.m_style.hilite_padding_y = 0;
  negative.m_ctx.cinfo.highlighted = 1; negative.screen.x = -31;
  negative.Move(30, 20); Check(negative.calls == 0, "first point at screen x=-1");
  negative.Move(31, 20); Check(negative.calls == 1, "x=-1 must not remain sentinel");
  WeaselPanel disabled; disabled.m_style.hover_type = UIStyle::NONE;
  disabled.Move(30, 20); disabled.Move(31, 20);
  Check(disabled.calls == 0, "disabled hover");
  CheckHoverEdges();
  CheckHoverOverlap();
  std::cout << "PASS hover: stationary screen cursor, skin origin, negative monitor, decoration hit bounds; 3 DPI edge matrices and production overlap semantics\n";
  return 0;
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
