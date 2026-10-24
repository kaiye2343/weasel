// Run the real OnMouseMove/OnLeftClickedUp/OnLeftClickedDown methods without a
// HWND or simulated mouse input.
#include <windows.h>
#include <windowsx.h>
#include <atlbase.h>
#include <atltypes.h>
#include <WeaselIPCData.h>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace ATL;
using namespace weasel;

// No real WinAPI timers in this translation unit.
UINT_PTR TestSetTimer(HWND, UINT_PTR id, UINT, TIMERPROC) noexcept { return id; }
BOOL TestKillTimer(HWND, UINT_PTR) noexcept { return TRUE; }
#define SetTimer TestSetTimer
#define KillTimer TestKillTimer

// Same alpha checks as WeaselUI/WeaselPanel.cpp.
#define COLORTRANSPARENT(color) ((color & 0xff000000) == 0)
#define COLORNOTTRANSPARENT(color) ((color & 0xff000000) != 0)

constexpr int AUTOREV_TIMER = 0x100;
constexpr size_t MAX_CANDIDATES_COUNT = 10;
struct LayoutFixture {
  std::vector<CRect> rectangles{CRect(0, 0, 100, 30), CRect(100, 0, 200, 30)};
  int offsetX = 0, offsetY = 0;
  CRect prePage{300, 0, 320, 30}, nextPage{320, 0, 340, 30};
  CRect GetCandidateRect(int i) {
    return rectangles.at(static_cast<size_t>(i));
  }
  CRect GetPrepageRect() { return prePage; }
  CRect GetNextpageRect() { return nextPage; }
};
class WeaselPanel {
 public:
  WeaselPanel() {
    m_status.composing = true;
    m_ctx.cinfo.highlighted = 0;
  }
  LRESULT OnMouseMove(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnLeftClickedUp(UINT, WPARAM, LPARAM, BOOL&);
  LRESULT OnLeftClickedDown(UINT, WPARAM, LPARAM, BOOL&);
  static VOID CALLBACK OnTimer(HWND, UINT, UINT_PTR, DWORD) {}
  void ClientToScreen(POINT* p) { p->x += screen.x; p->y += screen.y; }
  void InvalidateRect(const RECT*, bool) { ++invalidations; }
  void RedrawWindow() { ++redraws; }
  void DestroyWindow() { ++destroys; }
  void _CaptureRect(CRect) { ++captures; }
  int DPI_SCALE(int n) { return static_cast<int>(n * scale); }
  HWND m_hWnd = nullptr;
  float scale = 1.0f;
  UIStyle m_style;
  Context m_ctx;
  Status m_status;
  bool m_mouse_entry = true, m_istorepos = false, m_hasMousePos = false;
  bool hide_candidates = false;
  CPoint screen = CPoint(10, 20), m_skin_origin = CPoint(20, 10);
  CPoint m_lastMousePos = {-1, -1};
  size_t m_candidateCount = 2;
  int m_offsetys[10]{}, m_offsety_preedit = 0, m_hoverIndex = -1, calls = 0,
      invalidations = 0, redraws = 0, destroys = 0, captures = 0;
  float bar_scale_ = 1.0f;
  CRect rcw;
  LayoutFixture fixture;
  LayoutFixture* m_layout = &fixture;
  static UINT_PTR ptimer;
  std::vector<int> selected, highlighted, pages;
  std::function<void(size_t*, size_t*, bool*, bool*)> _UICallback =
      [this](size_t* select, size_t* highlight, bool* page, bool*) {
        ++calls;
        if (select)
          selected.push_back(static_cast<int>(*select));
        if (highlight) {
          highlighted.push_back(static_cast<int>(*highlight));
          m_ctx.cinfo.highlighted = static_cast<int>(*highlight);
        }
        if (page)
          pages.push_back(*page ? 1 : -1);
      };
  void Move(int x, int y) {
    BOOL handled = FALSE; OnMouseMove(WM_MOUSEMOVE, 0, MAKELPARAM(x, y), handled);
  }
  void Down(int x, int y) {
    BOOL handled = FALSE;
    OnLeftClickedDown(WM_LBUTTONDOWN, 0, MAKELPARAM(x, y), handled);
  }
  void Up(int x, int y) {
    BOOL handled = FALSE;
    OnLeftClickedUp(WM_LBUTTONUP, 0, MAKELPARAM(x, y), handled);
  }
};
UINT_PTR WeaselPanel::ptimer = 0;
static bool ClosedHit(const CRect& r, const CPoint& p) {
  return p.x >= r.left && p.y >= r.top && p.x <= r.right && p.y <= r.bottom;
}
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
void CheckClickContracts() {
  // (a) zero padding: Down selects candidate 1, Up commits it.
  {
    WeaselPanel p;
    p.m_style.hover_type = UIStyle::NONE;
    p.m_style.hilite_padding_x = p.m_style.hilite_padding_y = 0;
    const int ox = p.m_skin_origin.x, oy = p.m_skin_origin.y;
    p.Down(ox + 100, oy + 10);
    Check(p.highlighted.size() == 1 && p.highlighted[0] == 1 &&
              p.m_ctx.cinfo.highlighted == 1,
          "zero padding Down highlights candidate 1");
    p.Up(ox + 100, oy + 10);
    Check(p.selected.size() == 1 && p.selected[0] == 1 && p.destroys == 0,
          "zero padding Up commits candidate 1");
  }
  {
    WeaselPanel p;
    p.m_style.hilite_padding_x = p.m_style.hilite_padding_y = 0;
    const int ox = p.m_skin_origin.x, oy = p.m_skin_origin.y;
    p.Down(ox, oy);
    Check(p.calls == 0 && p.m_ctx.cinfo.highlighted == 0,
          "left/top edge is inclusive");
    p.Up(ox, oy);
    Check(p.selected.size() == 1 && p.selected[0] == 0,
          "left/top edge commits candidate 0");
  }
  {
    WeaselPanel p;
    p.m_style.hilite_padding_x = p.m_style.hilite_padding_y = 0;
    const int ox = p.m_skin_origin.x, oy = p.m_skin_origin.y;
    p.Down(ox, oy + 30);
    Check(p.calls == 0, "bottom edge is exclusive");
    p.Up(ox, oy + 30);
    Check(p.selected.empty(), "bottom edge does not commit");
  }
  {
    WeaselPanel p;
    p.m_style.hilite_padding_x = p.m_style.hilite_padding_y = 0;
    const int ox = p.m_skin_origin.x, oy = p.m_skin_origin.y;
    p.Down(ox + 100, oy);
    Check(p.highlighted.size() == 1 && p.highlighted[0] == 1,
          "right edge is exclusive and next left edge inclusive");
  }
  // (b) Down candidate 0 then Up candidate 1: highlight only, no commit.
  {
    WeaselPanel p;
    p.m_style.hilite_padding_x = p.m_style.hilite_padding_y = 0;
    p.m_ctx.cinfo.highlighted = 1;
    const int ox = p.m_skin_origin.x, oy = p.m_skin_origin.y;
    p.Down(ox + 50, oy + 10);
    Check(p.highlighted.size() == 1 && p.highlighted[0] == 0,
          "Down candidate 0 highlights it");
    p.Up(ox + 150, oy + 10);
    Check(p.selected.empty() && p.redraws == 1,
          "Up at candidate 1 only redraws");
  }
  // (c) overlap padding: Down picks first candidate, Up commits current 0.
  {
    WeaselPanel p;
    p.m_style.hilite_padding_x = 4;
    p.m_style.hilite_padding_y = 0;
    p.m_ctx.cinfo.highlighted = 1;
    const int ox = p.m_skin_origin.x, oy = p.m_skin_origin.y;
    p.Down(ox + 99, oy + 10);
    Check(p.highlighted.size() == 1 && p.highlighted[0] == 0,
          "Down overlap follows production first-candidate order");
    p.Up(ox + 99, oy + 10);
    Check(p.selected.size() == 1 && p.selected[0] == 0,
          "Up overlap commits current highlighted 0");
  }
  // (d) page/candidate overlap and page priority.
  {
    WeaselPanel p;
    p.m_style.inline_preedit = false;
    p.m_style.prevpage_color = p.m_style.nextpage_color = 0xff000000;
    p.m_ctx.cinfo.currentPage = 1;
    p.m_ctx.cinfo.is_last_page = false;
    p.fixture.prePage = CRect(0, 0, 100, 30);
    p.fixture.nextPage = CRect(100, 0, 200, 30);
    const int ox = p.m_skin_origin.x, oy = p.m_skin_origin.y;
    p.Down(ox + 50, oy + 10);
    Check(p.pages.size() == 1 && p.pages[0] == -1 && p.highlighted.empty(),
          "prev page wins overlap and records false as -1");
  }
  {
    WeaselPanel p;
    p.m_style.inline_preedit = false;
    p.m_style.prevpage_color = p.m_style.nextpage_color = 0xff000000;
    p.m_ctx.cinfo.currentPage = 0;
    p.m_ctx.cinfo.is_last_page = true;
    p.fixture.prePage = CRect(0, 0, 100, 30);
    p.m_ctx.cinfo.highlighted = 1;
    const int ox = p.m_skin_origin.x, oy = p.m_skin_origin.y;
    p.Down(ox + 50, oy + 10);
    Check(p.pages.empty() && p.highlighted.size() == 1 &&
              p.highlighted[0] == 0,
          "currentPage 0 disables prev and keeps candidate logic");
  }
  {
    WeaselPanel p;
    p.m_style.inline_preedit = false;
    p.m_style.prevpage_color = p.m_style.nextpage_color = 0xff000000;
    p.m_ctx.cinfo.currentPage = 0;
    p.m_ctx.cinfo.is_last_page = false;
    p.fixture.nextPage = CRect(100, 0, 200, 30);
    const int ox = p.m_skin_origin.x, oy = p.m_skin_origin.y;
    p.Down(ox + 150, oy + 10);
    Check(p.pages.size() == 1 && p.pages[0] == 1 && p.highlighted.empty(),
          "next page wins overlap and records true as 1");
  }
  {
    WeaselPanel p;
    p.m_style.inline_preedit = false;
    p.m_style.prevpage_color = p.m_style.nextpage_color = 0xff000000;
    p.m_ctx.cinfo.currentPage = 0;
    p.m_ctx.cinfo.is_last_page = true;
    p.fixture.nextPage = CRect(100, 0, 200, 30);
    const int ox = p.m_skin_origin.x, oy = p.m_skin_origin.y;
    p.Down(ox + 150, oy + 10);
    Check(p.pages.empty() && p.highlighted.size() == 1 &&
              p.highlighted[0] == 1,
          "is_last_page disables next and keeps candidate logic");
  }
  // (e) reposition offsets for candidates and prepage.
  {
    WeaselPanel p;
    p.m_istorepos = true;
    p.m_offsetys[1] = 7;
    p.m_style.hilite_padding_x = p.m_style.hilite_padding_y = 0;
    const int ox = p.m_skin_origin.x, oy = p.m_skin_origin.y;
    p.Down(ox + 150, oy + 20);
    Check(p.highlighted.size() == 1 && p.highlighted[0] == 1,
          "candidate offsetys converts the hit");
    p.Up(ox + 150, oy + 20);
    Check(p.selected.size() == 1 && p.selected[0] == 1,
          "candidate offsetys converts the release");
  }
  {
    WeaselPanel p;
    p.m_istorepos = true;
    p.m_offsetys[1] = 7;
    p.m_style.hilite_padding_x = p.m_style.hilite_padding_y = 0;
    const int ox = p.m_skin_origin.x, oy = p.m_skin_origin.y;
    p.Down(ox + 150, oy + 5);
    Check(p.calls == 0 && p.highlighted.empty(),
          "candidate offsetys excludes the point above the shifted rect");
  }
  {
    WeaselPanel p;
    p.m_istorepos = true;
    p.m_offsety_preedit = 5;
    p.m_style.inline_preedit = false;
    p.m_style.prevpage_color = p.m_style.nextpage_color = 0xff000000;
    p.m_ctx.cinfo.currentPage = 1;
    p.m_ctx.cinfo.is_last_page = true;
    const int ox = p.m_skin_origin.x, oy = p.m_skin_origin.y;
    p.Down(ox + 310, oy + 10);
    Check(p.pages.size() == 1 && p.pages[0] == -1,
          "prepage offsety_preedit converts the hit");
  }
  {
    WeaselPanel p;
    p.m_istorepos = true;
    p.m_offsety_preedit = 5;
    p.m_style.inline_preedit = false;
    p.m_style.prevpage_color = p.m_style.nextpage_color = 0xff000000;
    p.m_ctx.cinfo.currentPage = 1;
    p.m_ctx.cinfo.is_last_page = true;
    const int ox = p.m_skin_origin.x, oy = p.m_skin_origin.y;
    p.Down(ox + 310, oy + 2);
    Check(p.calls == 0 && p.pages.empty(),
          "prepage offsety_preedit excludes the point above the shifted rect");
  }
  // (f) hidden candidates and empty page.
  {
    WeaselPanel p;
    p.hide_candidates = true;
    p.m_style.hilite_padding_x = p.m_style.hilite_padding_y = 0;
    p.Down(30, 20);
    p.Up(30, 20);
    Check(p.calls == 0 && p.selected.empty() && p.highlighted.empty() &&
              p.pages.empty() && p.redraws == 0 && p.destroys == 0,
          "hide_candidates suppresses both click methods");
  }
  {
    WeaselPanel p;
    p.m_candidateCount = 0;
    p.m_style.hilite_padding_x = p.m_style.hilite_padding_y = 0;
    p.Down(30, 20);
    Check(p.calls == 0 && p.selected.empty() && p.highlighted.empty(),
          "empty page Down is safe");
  }
  // (g) DPI half-open matrix for click/release.
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
      p.m_style.hover_type = UIStyle::NONE;
      p.m_style.hilite_padding_x = 4;
      p.m_style.hilite_padding_y = 3;
      p.m_ctx.cinfo.highlighted = 0;
      p.m_skin_origin = CPoint(q(20), q(10));
      p.screen = CPoint(-q(600), q(40));
      p.m_istorepos = true;
      p.m_offsetys[1] = q(7);
      p.Down(point.x + p.m_skin_origin.x, point.y + p.m_skin_origin.y);
      if (point.inside) {
        Check(p.highlighted.size() == 1 && p.highlighted[0] == 1,
              "DPI click highlights the target candidate");
        p.Up(point.x + p.m_skin_origin.x, point.y + p.m_skin_origin.y);
        Check(p.selected.size() == 1 && p.selected[0] == 1,
              "DPI release commits the half-open hit");
      } else {
        Check(p.calls == 0 && p.highlighted.empty(),
              "DPI click outside has no highlight callback");
      }
    }
  }
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
  CheckClickContracts();
  std::cout << "PASS hover: stationary screen cursor, skin origin, negative monitor, decoration hit bounds; 3 DPI edge matrices and production overlap semantics; click/release/page contracts\n";
  return 0;
} catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
