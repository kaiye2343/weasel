// Run the real OnMouseMove method without a HWND or simulated mouse input.
#include <windows.h>
#include <windowsx.h>
#include <atlbase.h>
#include <atltypes.h>
#include <WeaselIPCData.h>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace ATL;
using namespace weasel;
constexpr size_t MAX_CANDIDATES_COUNT = 10;
struct LayoutFixture {
  CRect GetCandidateRect(int i) { return CRect(i * 100, 0, (i + 1) * 100, 30); }
};
class WeaselPanel {
 public:
  LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
  void ClientToScreen(POINT* p) { p->x += screen.x; p->y += screen.y; }
  void InvalidateRect(const RECT*, bool) { ++invalidations; }
  int DPI_SCALE(int n) { return n; }
  HWND m_hWnd = nullptr;
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
  std::cout << "PASS hover: stationary screen cursor, skin origin, negative monitor, decoration hit bounds\n";
  return 0;
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
