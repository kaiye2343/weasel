#include "stdafx.h"
#include "WeaselPanel.h"
#include "SkinGeometry.h"
#include "SkinImageLoader.h"
#include <WeaselSkin.h>
#include <fstream>

#include <utility>
#include <ShellScalingApi.h>
#include <VersionHelpers.hpp>
#include <WeaselIPCData.h>
#include <algorithm>

#include "VerticalLayout.h"
#include "HorizontalLayout.h"
#include "FullScreenLayout.h"
#include "VHorizontalLayout.h"

// for IDI_ZH, IDI_EN
#include <resource.h>
#define COLORTRANSPARENT(color) ((color & 0xff000000) == 0)
#define COLORNOTTRANSPARENT(color) ((color & 0xff000000) != 0)
#define TRANS_COLOR 0x00000000
#define GDPCOLOR_FROM_COLORREF(color)                                \
  Gdiplus::Color::MakeARGB(((color >> 24) & 0xff), GetRValue(color), \
                           GetGValue(color), GetBValue(color))
#define HALF_ALPHA_COLOR(color) \
  ((((color & 0xff000000) >> 25) & 0xff) << 24) | (color & 0x00ffffff)

#pragma comment(lib, "Shcore.lib")

template <class t0, class t1, class t2>
inline void LoadIconNecessary(t0& a, t1& b, t2& c, int d) {
  if (a == b)
    return;
  a = b;
  if (b.empty())
    c.LoadIconW(d, STATUS_ICON_SIZE, STATUS_ICON_SIZE, LR_DEFAULTCOLOR);
  else
    c = (HICON)LoadImage(NULL, b.c_str(), IMAGE_ICON, STATUS_ICON_SIZE,
                         STATUS_ICON_SIZE, LR_LOADFROMFILE);
}

static inline void ReconfigRoundInfo(IsToRoundStruct& rd,
                                     const int& i,
                                     const int& m_candidateCount) {
  if (i == 0 && m_candidateCount > 1) {
    std::swap(rd.IsTopLeftNeedToRound, rd.IsBottomLeftNeedToRound);
    std::swap(rd.IsTopRightNeedToRound, rd.IsBottomRightNeedToRound);
  }
  if (i == m_candidateCount - 1) {
    std::swap(rd.IsTopLeftNeedToRound, rd.IsBottomLeftNeedToRound);
    std::swap(rd.IsTopRightNeedToRound, rd.IsBottomRightNeedToRound);
  }
}

WeaselPanel::WeaselPanel(weasel::UI& ui)
    : m_layout(NULL),
      m_bg_image(NULL),
      m_ctx(ui.ctx()),
      m_octx(ui.octx()),
      m_status(ui.status()),
      m_style(ui.style()),
      m_ostyle(ui.ostyle()),
      m_candidateCount(0),
      m_current_zhung_icon(),
      m_inputPos(CRect()),
      m_sticky(false),
      dpi(96),
      hide_candidates(false),
      pDWR(ui.pdwr()),
      _UICallback(ui.uiCallback()),
      _m_gdiplusToken(0) {
  m_iconDisabled.LoadIconW(IDI_RELOAD, STATUS_ICON_SIZE, STATUS_ICON_SIZE,
                           LR_DEFAULTCOLOR);
  m_iconEnabled.LoadIconW(IDI_ZH, STATUS_ICON_SIZE, STATUS_ICON_SIZE,
                          LR_DEFAULTCOLOR);
  m_iconAlpha.LoadIconW(IDI_EN, STATUS_ICON_SIZE, STATUS_ICON_SIZE,
                        LR_DEFAULTCOLOR);
  m_iconFull.LoadIconW(IDI_FULL_SHAPE, STATUS_ICON_SIZE, STATUS_ICON_SIZE,
                       LR_DEFAULTCOLOR);
  m_iconHalf.LoadIconW(IDI_HALF_SHAPE, STATUS_ICON_SIZE, STATUS_ICON_SIZE,
                       LR_DEFAULTCOLOR);
  // for gdi+ drawings, initialization
  m_gdiplus_startup_status = GdiplusStartup(
      &_m_gdiplusToken, &_m_gdiplusStartupInput, NULL);

  HMONITOR hMonitor = MonitorFromRect(m_inputPos, MONITOR_DEFAULTTONEAREST);
  UINT dpiX = 96, dpiY = 96;
  if (hMonitor) {
    GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    m_hMonitor = hMonitor;
  }
  dpi = dpiX;
  _InitFontRes();
  m_ostyle = m_style;
}

WeaselPanel::~WeaselPanel() {
  _ReleaseSkinImages();
  // 位图要在 Gdiplus 关闭之前释放
  delete m_layout;
  m_layout = NULL;
  delete m_bg_image;
  m_bg_image = NULL;
  if (_m_gdiplusToken) Gdiplus::GdiplusShutdown(_m_gdiplusToken);
  // pDWR.reset();
}

void WeaselPanel::_ResizeWindow() {
  CDCHandle dc = GetDC();
  _UpdateSkinGeometry();
  CSize m_size = m_skin_size;
  SetWindowPos(NULL, 0, 0, m_size.cx, m_size.cy,
               SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOREDRAW);
  ReleaseDC(dc);
}

void WeaselPanel::_ReleaseSkinImages() {
  m_skin_bitmaps.clear();
  m_skin_scaled_bitmaps.clear(); m_skin_scaled_sizes.clear();
  std::vector<BYTE>().swap(m_skin_base_pixels);
  m_skin_config.clear();
  m_skin_rects.clear();
  m_skin_stamps.clear();
  m_skin_retry_after = 0; m_skin_load_failed = false;
}

void WeaselPanel::_UpdateSkinGeometry() {
  const auto& frame = m_style.skin_frame_image.empty() ? m_style.background_image : m_style.skin_frame_image;
  std::error_code frameError;
  const auto frameStamp = frame.empty() ? std::filesystem::file_time_type{} :
      std::filesystem::last_write_time(frame, frameError);
  if (frame != m_skin_frame_path || frameStamp != m_skin_frame_stamp) {
    m_skin_frame_path = frame; m_skin_frame_stamp = frameStamp;
    m_bg_image_stale = true; std::vector<BYTE>().swap(m_skin_base_pixels); ++m_skin_revision;
  }
  const CSize base = m_layout->GetContentSize();
  m_skin_origin = CPoint(0, 0);
  m_skin_size = base;
  m_skin_rects.clear();
  if (hide_candidates || !m_status.composing || !m_candidateCount ||
      m_style.layout_type != UIStyle::LAYOUT_HORIZONTAL)
    return;

  std::vector<std::filesystem::file_time_type> stamps;
  for (const auto& image : m_style.skin_images) {
    std::error_code error;
    stamps.push_back(std::filesystem::last_write_time(image.path, error));
  }
  if (m_skin_config != m_style.skin_images || stamps != m_skin_stamps) {
    std::vector<std::unique_ptr<Gdiplus::Bitmap>> next;
    bool valid = m_style.skin_images.empty() || GetTickCount64() >= m_skin_retry_after;
    std::string failure;
    for (const auto& image : m_style.skin_images) {
      if (!valid) break;
      SkinImageLoadError error;
      auto bitmap = LoadSkinBitmap(image.path.c_str(), error);
      if (!bitmap) {
        failure = "layer=" + std::to_string(next.size()) + " stage=" + error.stage +
            " gdip=" + std::to_string(error.status) +
            " file_error=" + std::to_string(error.file_error) +
            " dimensions=" + std::to_string(error.width) + "," + std::to_string(error.height);
        valid = false;
        break;
      }
      next.push_back(std::move(bitmap));
    }
    if (valid) {
      m_skin_config = m_style.skin_images; m_skin_bitmaps.swap(next);
      m_skin_scaled_bitmaps.clear(); m_skin_scaled_sizes.clear();
      std::vector<BYTE>().swap(m_skin_base_pixels);
      m_skin_stamps = std::move(stamps); ++m_skin_revision;
      m_skin_retry_after = 0; m_skin_load_failed = false;
      _TraceSkinState("resources_ready");
    } else if (GetTickCount64() >= m_skin_retry_after) {
      // Log each actual attempt (at most once/second), including recovery attempts.
      _TraceSkinState("decode_failed_preserving_previous_resources", failure);
      m_skin_load_failed = true; m_skin_retry_after = GetTickCount64() + 1000;
    }
  }
  SkinRect bounds{0, 0, base.cx, base.cy};
  const CRect visibleFrame = m_layout->GetContentRect();
  for (size_t i = 0; i < m_skin_config.size(); ++i) {
    const auto& image = m_skin_config[i];
    SkinRect rect = PlaceSkinImage(
        {visibleFrame.left, visibleFrame.top, visibleFrame.right, visibleFrame.bottom},
        image.anchor == L"top_right" || image.anchor == L"bottom_right",
        image.anchor == L"bottom_left" || image.anchor == L"bottom_right",
        DPI_SCALE(image.x), DPI_SCALE(image.y), DPI_SCALE(image.width),
        DPI_SCALE(image.height));
    m_skin_rects.push_back(CRect(rect.left, rect.top, rect.right, rect.bottom));
    if (m_skin_bitmaps[i])
      bounds = ExpandSkinCanvas(bounds, rect);
  }
  m_skin_origin = CPoint(-bounds.left, -bounds.top);
  m_skin_size = CSize(bounds.right - bounds.left, bounds.bottom - bounds.top);
}

void WeaselPanel::_CreateLayout() {
  if (m_layout != NULL)
    delete m_layout;

  Layout* layout = NULL;
  if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT) {
    layout = new VHorizontalLayout(m_style, m_ctx, m_status, pDWR);
  } else {
    if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL ||
        m_style.layout_type == UIStyle::LAYOUT_VERTICAL_FULLSCREEN) {
      layout = new VerticalLayout(m_style, m_ctx, m_status, pDWR);
    } else if (m_style.layout_type == UIStyle::LAYOUT_HORIZONTAL ||
               m_style.layout_type == UIStyle::LAYOUT_HORIZONTAL_FULLSCREEN) {
      layout = new HorizontalLayout(m_style, m_ctx, m_status, pDWR);
    }

    if (IS_FULLSCREENLAYOUT(m_style)) {
      layout = new FullScreenLayout(m_style, m_ctx, m_status, m_inputPos,
                                    layout, pDWR);
    }
  }
  m_layout = layout;
}

// 更新界面
void WeaselPanel::Refresh() {
  const bool styleChanged = m_style != m_ostyle;
  const bool wasHidden = hide_candidates;
  const UINT previousDpi = dpi;
  const CSize previousSize = m_skin_size;
  const CPoint previousOrigin = m_skin_origin;
  const size_t previousResources = m_skin_revision;
  bool should_show_icon =
      (m_status.ascii_mode || !m_status.composing || !m_ctx.aux.empty());
  m_candidateCount = static_cast<BYTE>((std::min)(m_ctx.cinfo.candies.size(), size_t(MAX_CANDIDATES_COUNT)));
  // check if to hide candidates window
  // show tips status, two kind of situation: 1) only aux strings, don't care
  // icon status; 2)only icon(ascii mode switching)
  bool show_tips =
      (!m_ctx.aux.empty() && m_ctx.cinfo.empty() && m_ctx.preedit.empty()) ||
      (m_ctx.empty() && should_show_icon);
  // show schema menu status: schema_id == L".default"
  bool show_schema_menu = m_status.schema_id == L".default";
  bool margin_negative =
      (DPI_SCALE(m_style.margin_x) < 0 || DPI_SCALE(m_style.margin_y) < 0);
  // when to hide_cadidates?
  // 1. margin_negative, and not in show tips mode( ascii switching / half-full
  // switching / simp-trad switching / error tips), and not in schema menu
  // 2. inline preedit without candidates
  bool inline_no_candidates =
      (m_style.inline_preedit && m_candidateCount == 0) && !show_tips;
  hide_candidates = inline_no_candidates ||
                    (margin_negative && !show_tips && !show_schema_menu);

  // only RedrawWindow if no need to hide candidates window, or
  // inline_no_candidates
  if (!hide_candidates || inline_no_candidates) {
    _InitFontRes();
    _CreateLayout();

    CDCHandle dc = GetDC();
    m_layout->DoLayout(dc, pDWR);
    ReleaseDC(dc);
    _ResizeWindow();
    _RepositionWindow();
    const bool geometryChanged = previousSize != m_skin_size || previousOrigin != m_skin_origin;
    if (m_ctx != m_octx || styleChanged || previousDpi != dpi || geometryChanged || previousResources != m_skin_revision || wasHidden != hide_candidates || m_redraw_by_monitor_change) {
      m_octx = m_ctx;
      if ((!m_style.skin_images.empty() || !m_skin_config.empty()) && (styleChanged || geometryChanged || previousDpi != dpi || wasHidden != hide_candidates))
        _TraceSkinState(styleChanged ? "style_redraw" : previousDpi != dpi ? "dpi_redraw" : "geometry_or_visibility_redraw");
      RedrawWindow();
    }
  }
}

void WeaselPanel::_InitFontRes(bool forced) {
  HMONITOR hMonitor = MonitorFromRect(m_inputPos, MONITOR_DEFAULTTONEAREST);
  UINT dpiX = 96, dpiY = 96;
  if (hMonitor)
    GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
  // prepare d2d1 resources
  // if style changed, or dpi changed, or pDWR NULL, re-initialize directwrite
  // resources
  if (forced || (pDWR == NULL) || (m_ostyle != m_style) || (dpiX != dpi)) {
    pDWR.reset();
    pDWR = std::make_shared<DirectWriteResources>(m_style, dpiX);
    pDWR->pRenderTarget->SetTextAntialiasMode(
        (D2D1_TEXT_ANTIALIAS_MODE)m_style.antialias_mode);
  }
  m_ostyle = m_style;
  dpi = dpiX;
  dpiScaleLayout = (float)dpi / 96.0f;
}

static HBITMAP CopyDCToBitmap(HDC hDC, LPRECT lpRect) {
  if (!hDC || !lpRect || IsRectEmpty(lpRect))
    return NULL;
  HDC hMemDC = NULL;
  HBITMAP hBitmap = NULL, hOldBitmap = NULL;
  int nX, nY, nX2, nY2;
  int nWidth, nHeight;

  nX = lpRect->left;
  nY = lpRect->top;
  nX2 = lpRect->right;
  nY2 = lpRect->bottom;
  nWidth = nX2 - nX;
  nHeight = nY2 - nY;

  hMemDC = CreateCompatibleDC(hDC);
  if (!hMemDC)
    return NULL;

  hBitmap = CreateCompatibleBitmap(hDC, nWidth, nHeight);
  if (!hBitmap) {
    DeleteDC(hMemDC);
    return NULL;
  }

  hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);
  if (!hOldBitmap) {
    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    return NULL;
  }

  if (!BitBlt(hMemDC, 0, 0, nWidth, nHeight, hDC, nX, nY, SRCCOPY)) {
    // restore and cleanup
    SelectObject(hMemDC, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    return NULL;
  }

  SelectObject(hMemDC, hOldBitmap);
  DeleteDC(hMemDC);
  return hBitmap;
}

void WeaselPanel::_CaptureRect(CRect& rect) {
  HDC ScreenDC = ::GetDC(NULL);
  CRect rc;
  GetWindowRect(&rc);
  POINT WindowPosAtScreen = {rc.left, rc.top};
  CRect captureRect = rect;
  captureRect.OffsetRect(WindowPosAtScreen);
  captureRect.OffsetRect(m_skin_origin);
  // create bitmap first (avoid holding clipboard while capturing)
  HBITMAP bmp = CopyDCToBitmap(ScreenDC, LPRECT(captureRect));
  if (!bmp) {
    ::ReleaseDC(NULL, ScreenDC);
    return;
  }

  // capture input window to clipboard
  if (!OpenClipboard()) {
    DEBUG << "_CaptureRect: OpenClipord ailed";
    DeleteObject(bmp);
    ::ReleaseDC(NULL, ScreenDC);
    return;
  }
  EmptyClipboard();
  if (!SetClipboardData(CF_BITMAP, bmp)) {
    DEBUG << "_CaptureRect: SetClipboardData failed";
    DeleteObject(bmp);
  }
  CloseClipboard();
  ::ReleaseDC(NULL, ScreenDC);
}

LRESULT WeaselPanel::OnMouseActivate(UINT uMsg,
                                     WPARAM wParam,
                                     LPARAM lParam,
                                     BOOL& bHandled) {
  bHandled = true;
  return MA_NOACTIVATE;
}

LRESULT WeaselPanel::OnMouseWheel(UINT uMsg,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  BOOL& bHandled) {
  int delta = GET_WHEEL_DELTA_WPARAM(wParam);
  if (_UICallback && delta != 0) {
    bool nextpage = delta < 0;
    _UICallback(NULL, NULL, NULL, &nextpage);
  }
  bHandled = true;
  return 0;
}

LRESULT WeaselPanel::OnLeftClickedUp(UINT uMsg,
                                     WPARAM wParam,
                                     LPARAM lParam,
                                     BOOL& bHandled) {
  if (hide_candidates) {
    bHandled = true;
    return 0;
  }
  CPoint point;
  point.x = GET_X_LPARAM(lParam) - m_skin_origin.x;
  point.y = GET_Y_LPARAM(lParam) - m_skin_origin.y;

  ::KillTimer(m_hWnd, AUTOREV_TIMER);
  bar_scale_ = 1.0;
  ptimer = 0;
  {
    // select by click
    CRect rect = m_layout->GetCandidateRect((int)m_ctx.cinfo.highlighted);
    if (m_istorepos)
      rect.OffsetRect(0, m_offsetys[m_ctx.cinfo.highlighted]);
    rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                     DPI_SCALE(m_style.hilite_padding_y));
    if (rect.PtInRect(point)) {
      size_t i = m_ctx.cinfo.highlighted;
      if (_UICallback) {
        m_mouse_entry = false;
        _UICallback(&i, NULL, NULL, NULL);
        if (!m_status.composing)
          DestroyWindow();
      }
    } else {
      RedrawWindow();
    }
  }
  bHandled = true;
  return 0;
}

LRESULT WeaselPanel::OnLeftClickedDown(UINT uMsg,
                                       WPARAM wParam,
                                       LPARAM lParam,
                                       BOOL& bHandled) {
  if (hide_candidates) {
    bHandled = true;
    return 0;
  }
  CPoint point;
  point.x = GET_X_LPARAM(lParam) - m_skin_origin.x;
  point.y = GET_Y_LPARAM(lParam) - m_skin_origin.y;

  // capture
  if (m_style.click_to_capture) {
    CRect recth = m_layout->GetCandidateRect((int)m_ctx.cinfo.highlighted);
    if (m_istorepos)
      recth.OffsetRect(0, m_offsetys[m_ctx.cinfo.highlighted]);
    recth.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                      DPI_SCALE(m_style.hilite_padding_y));
    // capture widow
    if (recth.PtInRect(point))
      _CaptureRect(recth);
    else {
      // if shadow_color transparent, decrease the capture rectangle size
      if (COLORTRANSPARENT(m_style.shadow_color) &&
          DPI_SCALE(m_style.shadow_radius) != 0) {
        CRect crc(rcw);
        int shadow_gap = (DPI_SCALE(m_style.shadow_offset_x) == 0 &&
                          DPI_SCALE(m_style.shadow_offset_y) == 0)
                             ? 2 * DPI_SCALE(m_style.shadow_radius)
                             : DPI_SCALE(m_style.shadow_radius) +
                                   DPI_SCALE(m_style.shadow_radius) / 2;
        int ofx = DPI_SCALE(m_style.hilite_padding_x) +
                              abs(DPI_SCALE(m_style.shadow_offset_x)) +
                              shadow_gap >
                          abs(DPI_SCALE(m_style.margin_x))
                      ? DPI_SCALE(m_style.hilite_padding_x) +
                            abs(DPI_SCALE(m_style.shadow_offset_x)) +
                            shadow_gap - abs(DPI_SCALE(m_style.margin_x))
                      : 0;
        int ofy = DPI_SCALE(m_style.hilite_padding_y) +
                              abs(DPI_SCALE(m_style.shadow_offset_y)) +
                              shadow_gap >
                          abs(DPI_SCALE(m_style.margin_y))
                      ? DPI_SCALE(m_style.hilite_padding_y) +
                            abs(DPI_SCALE(m_style.shadow_offset_y)) +
                            shadow_gap - abs(DPI_SCALE(m_style.margin_y))
                      : 0;
        crc.DeflateRect(m_layout->offsetX - ofx, m_layout->offsetY - ofy);
        _CaptureRect(crc);
      } else {
        _CaptureRect(rcw);
      }
    }
  }
  {
    if (!m_style.inline_preedit && m_candidateCount != 0 &&
        COLORNOTTRANSPARENT(m_style.prevpage_color) &&
        COLORNOTTRANSPARENT(m_style.nextpage_color)) {
      // click prepage
      if (m_ctx.cinfo.currentPage != 0) {
        CRect prc = m_layout->GetPrepageRect();
        if (m_istorepos)
          prc.OffsetRect(0, m_offsety_preedit);
        if (prc.PtInRect(point)) {
          bool nextPage = false;
          if (_UICallback)
            _UICallback(NULL, NULL, &nextPage, NULL);
          bHandled = true;
          return 0;
        }
      }
      // click nextpage
      if (!m_ctx.cinfo.is_last_page) {
        CRect prc = m_layout->GetNextpageRect();
        if (m_istorepos)
          prc.OffsetRect(0, m_offsety_preedit);
        if (prc.PtInRect(point)) {
          bool nextPage = true;
          if (_UICallback)
            _UICallback(NULL, NULL, &nextPage, NULL);
          bHandled = true;
          return 0;
        }
      }
    }
    // select by click relative actions
    for (size_t i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
      CRect rect = m_layout->GetCandidateRect((int)i);
      if (m_istorepos)
        rect.OffsetRect(0, m_offsetys[i]);
      rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                       DPI_SCALE(m_style.hilite_padding_y));
      if (rect.PtInRect(point)) {
        bar_scale_ = 0.8f;
        // modify highlighted
        if (i != m_ctx.cinfo.highlighted) {
          if (_UICallback)
            _UICallback(NULL, &i, NULL, NULL);
        } else {
          RedrawWindow();
        }
        ptimer = UINT_PTR(this);
        ::SetTimer(m_hWnd, AUTOREV_TIMER, 1000, &WeaselPanel::OnTimer);
        bHandled = true;
        return 0;
      }
    }
  }
  bHandled = true;
  return 0;
}

UINT_PTR WeaselPanel::ptimer = 0;
VOID CALLBACK WeaselPanel::OnTimer(_In_ HWND hwnd,
                                   _In_ UINT uMsg,
                                   _In_ UINT_PTR idEvent,
                                   _In_ DWORD dwTime) {
  ::KillTimer(hwnd, idEvent);
  WeaselPanel* self = (WeaselPanel*)ptimer;
  ptimer = 0;
  if (self) {
    self->bar_scale_ = 1.0;
    self->RedrawWindow();
  }
}

LRESULT WeaselPanel::OnMouseMove(UINT uMsg,
                                 WPARAM wParam,
                                 LPARAM lParam,
                                 BOOL& bHandled) {
  if (m_style.hover_type == UIStyle::NONE)
    return 0;
  if (m_mouse_entry == false) {
    TRACKMOUSEEVENT tme;
    tme.cbSize = sizeof(TRACKMOUSEEVENT);
    tme.dwFlags = TME_LEAVE;
    tme.dwHoverTime = 20;  // unit: ms
    tme.hwndTrack = m_hWnd;
    TrackMouseEvent(&tme);
  }
  bHandled = true;
  m_mouse_entry = true;
  CPoint point;
  point.x = GET_X_LPARAM(lParam) - m_skin_origin.x;
  point.y = GET_Y_LPARAM(lParam) - m_skin_origin.y;

  // Ignore if mouse screen position not changed
  CPoint ptScreen(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
  ClientToScreen(&ptScreen);
  const bool unchanged = m_hasMousePos && ptScreen == m_lastMousePos;
  const bool first = !m_hasMousePos;
  m_lastMousePos = ptScreen;
  m_hasMousePos = true;
  if (unchanged || first)
    return 0;

  for (size_t i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
    CRect rect = m_layout->GetCandidateRect((int)i);
    if (m_istorepos)
      rect.OffsetRect(0, m_offsetys[i]);
    rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                     DPI_SCALE(m_style.hilite_padding_y));
    if (rect.PtInRect(point)) {
      if (i != m_ctx.cinfo.highlighted) {
        if (m_style.hover_type == UIStyle::HoverType::HILITE) {
          if (_UICallback)
            _UICallback(NULL, &i, NULL, NULL);
        } else if (m_hoverIndex != i) {
          m_hoverIndex = static_cast<int>(i);
          InvalidateRect(&rcw, true);
        }
      } else if (m_style.hover_type == UIStyle::HoverType::SEMI_HILITE &&
                 m_hoverIndex != -1) {
        m_hoverIndex = -1;
        InvalidateRect(&rcw, true);
      }
    }
  }
  return 0;
}

LRESULT WeaselPanel::OnMouseLeave(UINT uMsg,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  BOOL& bHandled) {
  m_hoverIndex = -1;
  InvalidateRect(&rcw, true);
  m_mouse_entry = false;
  return 0;
}

// 背景图预缩门槛：长边 2048 或总像素 4M，两个条件都拦。
// 只卡长边会放过 4096×4096 这种解码后 67MB 的图
static const UINT kBgImageMaxSide = 2048;
static const double kBgImageMaxPixels = 4096.0 * 1024.0;

void WeaselPanel::_HighlightText(CDCHandle& dc,
                                 const CRect& rc,
                                 const COLORREF& color,
                                 const COLORREF& shadowColor,
                                 const int& radius,
                                 const BackType& type = BackType::TEXT,
                                 const IsToRoundStruct& rd = IsToRoundStruct(),
                                 const COLORREF& bordercolor = TRANS_COLOR) {
  // Graphics obj with SmoothingMode
  Gdiplus::Graphics g_back(dc);
  g_back.SetSmoothingMode(Gdiplus::SmoothingMode::SmoothingModeHighQuality);

  // blur buffer
  int blurMarginX = m_layout->offsetX;
  int blurMarginY = m_layout->offsetY;

  GraphicsRoundRectPath* hiliteBackPath;
  if (rd.Hemispherical && type != BackType::BACKGROUND &&
      NOT_FULLSCREENLAYOUT(m_style))
    hiliteBackPath = new GraphicsRoundRectPath(
        rc,
        DPI_SCALE(m_style.round_corner_ex) -
            (DPI_SCALE(m_style.border) % 2 ? DPI_SCALE(m_style.border) / 2 : 0),
        rd.IsTopLeftNeedToRound, rd.IsTopRightNeedToRound,
        rd.IsBottomRightNeedToRound, rd.IsBottomLeftNeedToRound);
  else  // background or current candidate background not out of window
        // background
    hiliteBackPath = new GraphicsRoundRectPath(rc, radius);

  // 必须shadow_color都是非完全透明色才做绘制, 全屏状态不绘制阴影保证响应速度
  if (DPI_SCALE(m_style.shadow_radius) && COLORNOTTRANSPARENT(shadowColor) &&
      NOT_FULLSCREENLAYOUT(m_style)) {
    CRect rect(blurMarginX + DPI_SCALE(m_style.shadow_offset_x),
               blurMarginY + DPI_SCALE(m_style.shadow_offset_y),
               rc.Width() + blurMarginX + DPI_SCALE(m_style.shadow_offset_x),
               rc.Height() + blurMarginY + DPI_SCALE(m_style.shadow_offset_y));
    BYTE r = GetRValue(shadowColor);
    BYTE g = GetGValue(shadowColor);
    BYTE b = GetBValue(shadowColor);
    BYTE alpha = (BYTE)((shadowColor >> 24) & 255);
    Gdiplus::Color shadow_color = Gdiplus::Color::MakeARGB(alpha, r, g, b);
    static Gdiplus::Bitmap* pBitmapDropShadow;
    pBitmapDropShadow = new Gdiplus::Bitmap((INT)rc.Width() + blurMarginX * 2,
                                            (INT)rc.Height() + blurMarginY * 2,
                                            PixelFormat32bppPARGB);

    Gdiplus::Graphics g_shadow(pBitmapDropShadow);
    g_shadow.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    // dropshadow, draw a roundrectangle to blur
    if (DPI_SCALE(m_style.shadow_offset_x) != 0 ||
        DPI_SCALE(m_style.shadow_offset_y) != 0) {
      GraphicsRoundRectPath shadow_path(rect, radius);
      Gdiplus::SolidBrush shadow_brush(shadow_color);
      g_shadow.FillPath(&shadow_brush, &shadow_path);
    }
    // round shadow, draw multilines as base round line
    else {
      int step = alpha / DPI_SCALE(m_style.shadow_radius) / 2;
      Gdiplus::Pen pen_shadow(shadow_color, (Gdiplus::REAL)1);
      for (int i = 0; i < DPI_SCALE(m_style.shadow_radius); i++) {
        GraphicsRoundRectPath round_path(rect, radius + 1 + i);
        g_shadow.DrawPath(&pen_shadow, &round_path);
        shadow_color = Gdiplus::Color::MakeARGB(alpha - i * step, r, g, b);
        pen_shadow.SetColor(shadow_color);
        rect.InflateRect(1, 1);
      }
    }
    DoGaussianBlur(pBitmapDropShadow, (float)DPI_SCALE(m_style.shadow_radius),
                   (float)DPI_SCALE(m_style.shadow_radius));

    g_back.DrawImage(pBitmapDropShadow, rc.left - blurMarginX,
                     rc.top - blurMarginY);

    // free memory
    delete pBitmapDropShadow;
    pBitmapDropShadow = NULL;
  }

  // 必须back_color非完全透明才绘制
  if (COLORNOTTRANSPARENT(color)) {
    Gdiplus::Color back_color = GDPCOLOR_FROM_COLORREF(color);
    Gdiplus::SolidBrush back_brush(back_color);
    g_back.FillPath(&back_brush, hiliteBackPath);
  }
  // 候选窗背景图：填色之后按同一圆角路径裁进来画一层，压在候选文字之下
  // 只在整窗背景这一步绘制，候选词高亮不贴图；全屏布局跳过
  const auto& background = m_style.skin_frame_image.empty()
                               ? m_style.background_image
                               : m_style.skin_frame_image;
  if (type == BackType::BACKGROUND && !background.empty() &&
      NOT_FULLSCREENLAYOUT(m_style)) {
    // 按路径缓存原图：换宽高不重读盘，缩放在绘制时计算。
    // 图已被释放（关窗后重开）或路径变了都必须重载——
    // 只比路径会把「图没了」误判成「已经加载过」。
    if (m_bg_image == NULL || m_bg_image_path != background || m_bg_image_stale) {
      auto* previous = m_bg_image;
      const auto previousPath = m_bg_image_path;
      m_bg_image = NULL;
      Gdiplus::Bitmap* src = nullptr;
      if (!m_style.skin_frame_image.empty()) {
        // Skin packages can be replaced while active. Detach PNG pixels from
        // the source file, as for decoration layers, before caching the frame.
        SkinImageLoadError error;
        src = LoadSkinBitmap(background.c_str(), error).release();
      } else {
        src = Gdiplus::Bitmap::FromFile(background.c_str());
      }
      if (src && src->GetLastStatus() == Gdiplus::Ok) {
        // 加载成功才记账
        m_bg_image_path = background;
        UINT sw = src->GetWidth(), sh = src->GetHeight();
        // 超大图先缩一道再保留，避免解码后常驻内存过大
        if (sw > kBgImageMaxSide || sh > kBgImageMaxSide ||
            (double)sw * sh > kBgImageMaxPixels) {
          double k = (double)kBgImageMaxSide / (double)(sw > sh ? sw : sh);
          UINT dw = (UINT)(sw * k), dh = (UINT)(sh * k);
          Gdiplus::Bitmap* scaled =
              new Gdiplus::Bitmap(dw, dh, PixelFormat32bppPARGB);
          if (scaled->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics g_pre(scaled);
            g_pre.SetInterpolationMode(
                Gdiplus::InterpolationModeHighQualityBicubic);
            g_pre.DrawImage(src, 0, 0, dw, dh);
            m_bg_image = scaled;
          } else {
            delete scaled;
          }
        } else {
          m_bg_image = src;
          src = NULL;
        }
      } else {
        // 加载失败不留账：路径不进缓存，下一帧还会重试一次
        m_bg_image_path.clear();
      }
      delete src;
      if (m_bg_image) {
        delete previous;
        m_bg_image_stale = false;
      } else {
        m_bg_image = previous; m_bg_image_path = previousPath;
        m_bg_image_stale = true;
      }
    }
    if (m_bg_image) {
      _DrawBackgroundImage(g_back, rc, hiliteBackPath);
    }
  }
  // draw border, for bordercolor not transparent and border valid
  if (COLORNOTTRANSPARENT(bordercolor) && DPI_SCALE(m_style.border) > 0 &&
      (type != BackType::BACKGROUND ||
       (m_style.skin_border_style != 0 && m_style.skin_border_inset == 0))) {
    Gdiplus::Color border_color = GDPCOLOR_FROM_COLORREF(bordercolor);
    Gdiplus::Pen gPenBorder(border_color,
                            (Gdiplus::REAL)DPI_SCALE(m_style.border));
    if (type == BackType::BACKGROUND && m_style.skin_border_style == 2)
      gPenBorder.SetDashStyle(Gdiplus::DashStyleDash);
    // candidate window border
    if (type == BackType::BACKGROUND) {
      GraphicsRoundRectPath bgPath(rc, DPI_SCALE(m_style.round_corner_ex));
      g_back.DrawPath(&gPenBorder, &bgPath);
    } else if (type !=
               BackType::TEXT)  // hilited_candidate_border / candidate_border
      g_back.DrawPath(&gPenBorder, hiliteBackPath);
  }
  if (type == BackType::BACKGROUND && m_style.skin_border_style != 0 &&
      m_style.skin_border_inset > 0) {
    CRect inner = rc;
    inner.DeflateRect(DPI_SCALE(m_style.skin_border_inset),
                      DPI_SCALE(m_style.skin_border_inset));
    if (inner.Width() > 0 && inner.Height() > 0) {
      GraphicsRoundRectPath innerPath(inner, DPI_SCALE(m_style.round_corner));
      Gdiplus::Pen stitch(GDPCOLOR_FROM_COLORREF(m_style.border_color),
                          (Gdiplus::REAL)DPI_SCALE(m_style.border));
      if (m_style.skin_border_style == 2)
        stitch.SetDashStyle(Gdiplus::DashStyleDash);
      g_back.DrawPath(&stitch, &innerPath);
    }
  }
  // free memory
  delete hiliteBackPath;
  hiliteBackPath = NULL;
}

// 按 style/background_fill_mode 铺底图
void WeaselPanel::_DrawBackgroundImage(Gdiplus::Graphics& g,
                                       const CRect& rc,
                                       Gdiplus::GraphicsPath* clip_path) {
  if (!m_bg_image)
    return;
  if (!m_style.skin_frame_image.empty() &&
      m_style.skin_frame_slice.size() == 4) {
    const auto& cuts = m_style.skin_frame_slice;
    int sw = (int)m_bg_image->GetWidth(), sh = (int)m_bg_image->GetHeight();
    if (cuts[0] + cuts[2] >= sw || cuts[1] + cuts[3] >= sh)
      return;
    int left = (std::min)(DPI_SCALE(cuts[0]), rc.Width() / 2);
    int right = (std::min)(DPI_SCALE(cuts[2]), rc.Width() / 2);
    int top = (std::min)(DPI_SCALE(cuts[1]), rc.Height() / 2);
    int bottom = (std::min)(DPI_SCALE(cuts[3]), rc.Height() / 2);
    int sx[] = {0, cuts[0], sw - cuts[2], sw};
    int sy[] = {0, cuts[1], sh - cuts[3], sh};
    int dx[] = {rc.left, rc.left + left, rc.right - right, rc.right};
    int dy[] = {rc.top, rc.top + top, rc.bottom - bottom, rc.bottom};
    auto saved = g.Save();
    g.SetClip(clip_path);
    for (int y = 0; y < 3; ++y)
      for (int x = 0; x < 3; ++x)
        if (sx[x + 1] > sx[x] && sy[y + 1] > sy[y])
          g.DrawImage(
              m_bg_image,
              Gdiplus::Rect(dx[x], dy[y], dx[x + 1] - dx[x], dy[y + 1] - dy[y]),
              sx[x], sy[y], sx[x + 1] - sx[x], sy[y + 1] - sy[y],
              Gdiplus::UnitPixel);
    g.Restore(saved);
    return;
  }
  // 大图缩到候选窗尺寸要高质量重采样，不然发虚
  Gdiplus::InterpolationMode prev_interp = g.GetInterpolationMode();
  g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  g.SetClip(clip_path);
  const Gdiplus::REAL w = (Gdiplus::REAL)m_bg_image->GetWidth();
  const Gdiplus::REAL h = (Gdiplus::REAL)m_bg_image->GetHeight();
  const Gdiplus::RectF dst((Gdiplus::REAL)rc.left, (Gdiplus::REAL)rc.top,
                           (Gdiplus::REAL)rc.Width(),
                           (Gdiplus::REAL)rc.Height());
  switch (m_style.background_fill_mode) {
    case 1: {  // tile：原尺寸平铺，不缩放
      Gdiplus::TextureBrush brush(m_bg_image, Gdiplus::WrapModeTile);
      brush.TranslateTransform(dst.X, dst.Y);
      g.FillRectangle(&brush, dst);
      break;
    }
    case 2: {  // center：原尺寸居中，图大就自然被裁掉四边
      const Gdiplus::RectF box(dst.X + (dst.Width - w) / 2,
                               dst.Y + (dst.Height - h) / 2, w, h);
      g.DrawImage(m_bg_image, box);
      break;
    }
    case 3: {  // fit：等比缩放到整幅装得下，居中，四周留底色
      const Gdiplus::REAL k =
          (dst.Width / w < dst.Height / h) ? dst.Width / w : dst.Height / h;
      const Gdiplus::REAL dw = w * k, dh = h * k;
      g.DrawImage(m_bg_image,
                  Gdiplus::RectF(dst.X + (dst.Width - dw) / 2,
                                 dst.Y + (dst.Height - dh) / 2, dw, dh));
      break;
    }
    default:  // 0 = stretch：与旧行为逐像素一致
      g.DrawImage(m_bg_image, rc.left, rc.top, rc.Width(), rc.Height());
      break;
  }
  g.ResetClip();
  g.SetInterpolationMode(prev_interp);
}

// draw preedit text, text only
bool WeaselPanel::_DrawPreedit(const Text& text,
                               CDCHandle dc,
                               const CRect& rc) {
  bool drawn = false;
  std::wstring const& t = text.str;
  IDWriteTextFormat1* txtFormat = pDWR->pPreeditTextFormat.Get();

  if (!t.empty()) {
    weasel::TextRange range = m_layout->GetPreeditRange();

    if (range.start < range.end) {
      std::wstring before_str = t.substr(0, range.start);
      std::wstring hilited_str = t.substr(range.start, range.end);
      std::wstring after_str = t.substr(range.end);
      CSize beforeSz = m_layout->GetBeforeSize();
      CSize hilitedSz = m_layout->GetHilitedSize();
      CSize afterSz = m_layout->GetAfterSize();

      int x = rc.left;
      int y = rc.top;

      if (range.start > 0) {
        // zzz
        std::wstring str_before(t.substr(0, range.start));
        CRect rc_before;
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          rc_before = CRect(rc.left, y, rc.right, y + beforeSz.cy);
        else
          rc_before = CRect(x, rc.top, rc.left + beforeSz.cx, rc.bottom);
        _TextOut(rc_before, str_before.c_str(), str_before.length(),
                 m_style.text_color, txtFormat);
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          y += beforeSz.cy + DPI_SCALE(m_style.hilite_spacing);
        else
          x += beforeSz.cx + DPI_SCALE(m_style.hilite_spacing);
      }
      {
        // zzz[yyy]
        std::wstring str_highlight(
            t.substr(range.start, (size_t)range.end - range.start));
        CRect rc_hi;

        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          rc_hi = CRect(rc.left, y, rc.right, y + hilitedSz.cy);
        else
          rc_hi = CRect(x, rc.top, x + hilitedSz.cx, rc.bottom);
        _TextOut(rc_hi, str_highlight.c_str(), str_highlight.length(),
                 m_style.hilited_text_color, txtFormat);
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          y += rc_hi.Height() + DPI_SCALE(m_style.hilite_spacing);
        else
          x += rc_hi.Width() + DPI_SCALE(m_style.hilite_spacing);
      }
      if (range.end < static_cast<int>(t.length())) {
        // zzz[yyy]xxx
        std::wstring str_after(t.substr(range.end));
        CRect rc_after;
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          rc_after = CRect(rc.left, y, rc.right, y + afterSz.cy);
        else
          rc_after = CRect(x, rc.top, x + afterSz.cx, rc.bottom);
        _TextOut(rc_after, str_after.c_str(), str_after.length(),
                 m_style.text_color, txtFormat);
      }
    } else {
      CRect rcText(rc.left, rc.top, rc.right, rc.bottom);
      _TextOut(rcText, t.c_str(), t.length(), m_style.text_color, txtFormat);
    }
    // draw pager mark if not inline_preedit if necessary
    if (m_candidateCount && !m_style.inline_preedit &&
        COLORNOTTRANSPARENT(m_style.prevpage_color) &&
        COLORNOTTRANSPARENT(m_style.nextpage_color)) {
      const std::wstring pre = L"<";
      const std::wstring next = L">";
      CRect prc = m_layout->GetPrepageRect();
      // clickable color / disabled color
      int color =
          m_ctx.cinfo.currentPage ? m_style.prevpage_color : m_style.text_color;
      if (m_istorepos)
        prc.OffsetRect(0, m_offsety_preedit);
      _TextOut(prc, pre.c_str(), pre.length(), color, txtFormat);

      CRect nrc = m_layout->GetNextpageRect();
      // clickable color / disabled color
      color = m_ctx.cinfo.is_last_page ? m_style.text_color
                                       : m_style.nextpage_color;
      if (m_istorepos)
        nrc.OffsetRect(0, m_offsety_preedit);
      _TextOut(nrc, next.c_str(), next.length(), color, txtFormat);
    }
    drawn = true;
  }
  return drawn;
}

// draw hilited back color, back only
bool WeaselPanel::_DrawPreeditBack(const Text& text,
                                   CDCHandle dc,
                                   const CRect& rc) {
  bool drawn = false;
  std::wstring const& t = text.str;
  IDWriteTextFormat1* txtFormat = pDWR->pPreeditTextFormat.Get();

  if (!t.empty()) {
    weasel::TextRange range = m_layout->GetPreeditRange();

    if (range.start < range.end) {
      CSize beforeSz = m_layout->GetBeforeSize();
      CSize hilitedSz = m_layout->GetHilitedSize();

      int x = rc.left;
      int y = rc.top;

      if (range.start > 0) {
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          y += beforeSz.cy + DPI_SCALE(m_style.hilite_spacing);
        else
          x += beforeSz.cx + DPI_SCALE(m_style.hilite_spacing);
      }
      {
        CRect rc_hi;
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          rc_hi = CRect(rc.left, y, rc.right, y + hilitedSz.cy);
        else
          rc_hi = CRect(x, rc.top, x + hilitedSz.cx, rc.bottom);
        // if preedit rect size smaller than icon, fill the gap to
        // STATUS_ICON_SIZE
        if (m_layout->ShouldDisplayStatusIcon()) {
          if ((m_style.layout_type == UIStyle::LAYOUT_HORIZONTAL ||
               m_style.layout_type == UIStyle::LAYOUT_VERTICAL) &&
              hilitedSz.cy < STATUS_ICON_SIZE)
            rc_hi.InflateRect(0, (STATUS_ICON_SIZE - hilitedSz.cy) / 2);
          if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT &&
              hilitedSz.cx < STATUS_ICON_SIZE)
            rc_hi.InflateRect((STATUS_ICON_SIZE - hilitedSz.cx) / 2, 0);
        }

        rc_hi.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                          DPI_SCALE(m_style.hilite_padding_y));
        IsToRoundStruct rd = m_layout->GetTextRoundInfo();
        if (m_istorepos) {
          std::swap(rd.IsTopLeftNeedToRound, rd.IsBottomLeftNeedToRound);
          std::swap(rd.IsTopRightNeedToRound, rd.IsBottomRightNeedToRound);
        }
        _HighlightText(dc, rc_hi, m_style.hilited_back_color,
                       m_style.hilited_shadow_color,
                       DPI_SCALE(m_style.round_corner), BackType::TEXT, rd);
      }
    }
    drawn = true;
  }
  return drawn;
}

bool WeaselPanel::_DrawCandidates(CDCHandle& dc, bool back) {
  bool drawn = false;
  const std::vector<Text>& candidates(m_ctx.cinfo.candies);
  const std::vector<Text>& comments(m_ctx.cinfo.comments);
  const std::vector<Text>& labels(m_ctx.cinfo.labels);
  // prevent all text format nullptr
  if (pDWR->pTextFormat.Get() == nullptr &&
      pDWR->pLabelTextFormat.Get() == nullptr &&
      pDWR->pCommentTextFormat.Get() == nullptr) {
    _InitFontRes(true);
  }
  ComPtr<IDWriteTextFormat1> txtFormat = pDWR->pTextFormat;
  ComPtr<IDWriteTextFormat1> labeltxtFormat = pDWR->pLabelTextFormat;
  ComPtr<IDWriteTextFormat1> commenttxtFormat = pDWR->pCommentTextFormat;
  BackType bkType = BackType::CAND;

  CRect rect;
  // draw back color and shadow color, with gdi+
  if (back) {
    // if candidate_shadow_color not transparent, draw candidate shadow first
    if (COLORNOTTRANSPARENT(m_style.candidate_shadow_color)) {
      for (auto i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
        if (i == m_ctx.cinfo.highlighted || i == m_hoverIndex)
          continue;  // draw non hilited candidates only
        rect = m_layout->GetCandidateRect((int)i);
        IsToRoundStruct rd = m_layout->GetRoundInfo(i);
        if (m_istorepos) {
          rect.OffsetRect(0, m_offsetys[i]);
          ReconfigRoundInfo(rd, i, m_candidateCount);
        }
        rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                         DPI_SCALE(m_style.hilite_padding_y));
        _HighlightText(dc, rect, 0x00000000, m_style.candidate_shadow_color,
                       DPI_SCALE(m_style.round_corner), bkType, rd);
        drawn = true;
      }
    }
    // draw non highlighted candidates, without shadow
    if ((COLORNOTTRANSPARENT(m_style.candidate_back_color) ||
         COLORNOTTRANSPARENT(m_style.candidate_border_color))) {
      for (auto i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
        if (i == m_ctx.cinfo.highlighted || i == m_hoverIndex)
          continue;
        rect = m_layout->GetCandidateRect((int)i);
        IsToRoundStruct rd = m_layout->GetRoundInfo(i);
        if (m_istorepos) {
          rect.OffsetRect(0, m_offsetys[i]);
          ReconfigRoundInfo(rd, i, m_candidateCount);
        }
        rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                         DPI_SCALE(m_style.hilite_padding_y));
        _HighlightText(dc, rect, m_style.candidate_back_color, 0x00000000,
                       DPI_SCALE(m_style.round_corner), bkType, rd,
                       m_style.candidate_border_color);
        drawn = true;
      }
    }
    // draw semi-hilite background and shadow
    if (m_hoverIndex >= 0) {
      rect = m_layout->GetCandidateRect(m_hoverIndex);
      IsToRoundStruct rd = m_layout->GetRoundInfo(m_hoverIndex);
      if (m_istorepos) {
        rect.OffsetRect(0, m_offsetys[m_hoverIndex]);
        ReconfigRoundInfo(rd, m_hoverIndex, m_candidateCount);
      }
      rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                       DPI_SCALE(m_style.hilite_padding_y));
      _HighlightText(dc, rect,
                     HALF_ALPHA_COLOR(m_style.hilited_candidate_back_color),
                     HALF_ALPHA_COLOR(m_style.hilited_candidate_shadow_color),
                     DPI_SCALE(m_style.round_corner), bkType, rd,
                     HALF_ALPHA_COLOR(m_style.hilited_candidate_border_color));
    }
    // draw highlighted background and shadow
    {
      rect = m_layout->GetHighlightRect();
      bool markSt = bar_scale_ == 1.0 || (!m_style.mark_text.empty());
      IsToRoundStruct rd = m_layout->GetRoundInfo(m_ctx.cinfo.highlighted);
      if (m_istorepos) {
        rect.OffsetRect(0, m_offsetys[m_ctx.cinfo.highlighted]);
        ReconfigRoundInfo(rd, m_ctx.cinfo.highlighted, m_candidateCount);
      }
      rect.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                       DPI_SCALE(m_style.hilite_padding_y));
      _HighlightText(dc, rect, m_style.hilited_candidate_back_color,
                     markSt ? m_style.hilited_candidate_shadow_color : 0,
                     DPI_SCALE(m_style.round_corner), bkType, rd,
                     m_style.hilited_candidate_border_color);
      if (m_style.mark_text.empty() &&
          COLORNOTTRANSPARENT(m_style.hilited_mark_color)) {
        int height =
            min(rect.Height() - DPI_SCALE(m_style.hilite_padding_y) * 2,
                rect.Height() - DPI_SCALE(m_style.round_corner) * 2);
        int width = min(rect.Width() - DPI_SCALE(m_style.hilite_padding_x) * 2,
                        rect.Width() - DPI_SCALE(m_style.round_corner) * 2);
        width = min(width, static_cast<int>(rect.Width() * 0.618));
        height = min(height, static_cast<int>(rect.Height() * 0.618));
        if (bar_scale_ != 1.0f) {
          width = static_cast<int>(width * bar_scale_);
          height = static_cast<int>(height * bar_scale_);
        }
        Gdiplus::Graphics g_back(dc);
        g_back.SetSmoothingMode(
            Gdiplus::SmoothingMode::SmoothingModeHighQuality);
        Gdiplus::Color mark_color =
            GDPCOLOR_FROM_COLORREF(m_style.hilited_mark_color);
        Gdiplus::SolidBrush mk_brush(mark_color);
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT) {
          int x = rect.left + (rect.Width() - width) / 2;
          CRect mkrc{x, rect.top, x + width, rect.top + m_layout->mark_height};
          GraphicsRoundRectPath mk_path(mkrc, mkrc.Height() / 2);
          g_back.FillPath(&mk_brush, &mk_path);
        } else {
          int y = rect.top + (rect.Height() - height) / 2;
          CRect mkrc{rect.left, y, rect.left + m_layout->mark_width,
                     y + height};
          GraphicsRoundRectPath mk_path(mkrc, mkrc.Width() / 2);
          g_back.FillPath(&mk_brush, &mk_path);
        }
      }
      drawn = true;
    }
  }
  // draw text with direct write
  else {
    // begin draw candidate texts
    int label_text_color, candidate_text_color, comment_text_color;
    for (auto i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
      if (i == m_ctx.cinfo.highlighted || i == m_hoverIndex) {
        label_text_color = m_style.hilited_label_text_color;
        candidate_text_color = m_style.hilited_candidate_text_color;
        comment_text_color = m_style.hilited_comment_text_color;
      } else {
        label_text_color = m_style.label_text_color;
        candidate_text_color = m_style.candidate_text_color;
        comment_text_color = m_style.comment_text_color;
      }
      // Draw label
      std::wstring label = m_layout->GetLabelText(
          labels, (int)i, m_style.label_text_format.c_str());
      if (!label.empty()) {
        rect = m_layout->GetCandidateLabelRect((int)i);
        if (m_istorepos)
          rect.OffsetRect(0, m_offsetys[i]);
        _TextOut(rect, label.c_str(), label.length(), label_text_color,
                 labeltxtFormat.Get());
      }
      // Draw text
      std::wstring text = candidates.at(i).str;
      if (!text.empty()) {
        rect = m_layout->GetCandidateTextRect((int)i);
        if (m_istorepos)
          rect.OffsetRect(0, m_offsetys[i]);
        _TextOut(rect, text.c_str(), text.length(), candidate_text_color,
                 txtFormat.Get());
      }
      // Draw comment
      std::wstring comment = comments.at(i).str;
      if (m_style.skin_supports_annotation && m_style.annotation_position != 2 && !comment.empty() && COLORNOTTRANSPARENT(comment_text_color)) {
        rect = m_layout->GetCandidateCommentRect((int)i);
        if (m_istorepos)
          rect.OffsetRect(0, m_offsetys[i]);
        _TextOut(rect, comment.c_str(), comment.length(), comment_text_color,
                 commenttxtFormat.Get());
      }
      drawn = true;
    }
    // draw highlight mark
    {
      if (!m_style.mark_text.empty() &&
          COLORNOTTRANSPARENT(m_style.hilited_mark_color)) {
        CRect rc = m_layout->GetHighlightRect();
        if (m_istorepos)
          rc.OffsetRect(0, m_offsetys[m_ctx.cinfo.highlighted]);
        rc.InflateRect(DPI_SCALE(m_style.hilite_padding_x),
                       DPI_SCALE(m_style.hilite_padding_y));
        int vgap = m_layout->mark_height
                       ? (rc.Height() - m_layout->mark_height) / 2
                       : 0;
        int hgap =
            m_layout->mark_width ? (rc.Width() - m_layout->mark_width) / 2 : 0;
        CRect hlRc;
        if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT)
          hlRc = CRect(rc.left + hgap,
                       rc.top + DPI_SCALE(m_style.hilite_padding_y),
                       rc.left + hgap + m_layout->mark_width,
                       rc.top + DPI_SCALE(m_style.hilite_padding_y) +
                           m_layout->mark_height);
        else
          hlRc = CRect(rc.left + DPI_SCALE(m_style.hilite_padding_x),
                       rc.top + vgap,
                       rc.left + DPI_SCALE(m_style.hilite_padding_x) +
                           m_layout->mark_width,
                       rc.bottom - vgap);
        _TextOut(hlRc, m_style.mark_text.c_str(), m_style.mark_text.length(),
                 m_style.hilited_mark_color, pDWR->pTextFormat.Get());
      }
    }
  }
  return drawn;
}

// draw client area
void WeaselPanel::DoPaint(CDCHandle dc) {
  // turn off WS_EX_TRANSPARENT, for better resp performance
  ModifyStyleEx(WS_EX_TRANSPARENT, WS_EX_LAYERED);
  GetClientRect(&rcw);
  const CSize base_size = m_layout->GetContentSize();
  rcw = CRect(0, 0, base_size.cx, base_size.cy);
  // prepare memDC
  CDCHandle hdc = ::GetDC(m_hWnd);
  CDCHandle memDC = ::CreateCompatibleDC(hdc);
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = rcw.Width();
  info.bmiHeader.biHeight = -rcw.Height();
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* pixels = NULL;
  HBITMAP memBitmap =
      ::CreateDIBSection(hdc, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
  if (!memBitmap) {
    ::DeleteDC(memDC);
    ReleaseDC(hdc);
    return;
  }
  memset(pixels, 0, (size_t)rcw.Width() * rcw.Height() * 4);
  HGDIOBJ oldBitmap = ::SelectObject(memDC, memBitmap);
  ReleaseDC(hdc);
  bool drawn = false;
  if (!hide_candidates) {
    CRect auxrc = m_layout->GetAuxiliaryRect();
    CRect preeditrc = m_layout->GetPreeditRect();
    if (m_istorepos) {
      CRect* rects = new CRect[m_candidateCount];
      int* btmys = new int[m_candidateCount];
      for (auto i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
        rects[i] = m_layout->GetCandidateRect(i);
        btmys[i] = rects[i].bottom;
      }
      if (m_candidateCount) {
        if (!m_layout->IsInlinePreedit() && !m_ctx.preedit.str.empty())
          m_offsety_preedit =
              rects[m_candidateCount - 1].bottom - preeditrc.bottom;
        if (!m_ctx.aux.str.empty())
          m_offsety_aux = rects[m_candidateCount - 1].bottom - auxrc.bottom;
      } else {
        m_offsety_preedit = 0;
        m_offsety_aux = 0;
      }
      int base_gap = 0;
      if (!m_ctx.aux.str.empty())
        base_gap = auxrc.Height() + m_style.spacing;
      else if (!m_layout->IsInlinePreedit() && !m_ctx.preedit.str.empty())
        base_gap = preeditrc.Height() + m_style.spacing;

      for (auto i = 0; i < m_candidateCount && i < MAX_CANDIDATES_COUNT; ++i) {
        if (i == 0)
          m_offsetys[i] =
              btmys[m_candidateCount - i - 1] - base_gap - rects[i].bottom;
        else
          m_offsetys[i] = (rects[i - 1].top + m_offsetys[i - 1] -
                           DPI_SCALE(m_style.candidate_spacing)) -
                          rects[i].bottom;
      }
      delete[] rects;
      delete[] btmys;
    }
    // Cache only the fixed base. Candidate highlights, annotations and text stay live.
    const bool background = (!m_ctx.empty() && !m_style.inline_preedit) ||
        (m_style.inline_preedit && (m_candidateCount || !m_ctx.aux.empty()));
    const CRect backrc = m_layout->GetContentRect();
    const CSize offset(m_layout->offsetX, m_layout->offsetY);
    const size_t baseBytes = size_t(rcw.Width()) * rcw.Height() * 4;
    const bool cacheBase = !GetPropW(m_hWnd, L"Weasel.DisableStaticSkinCache");
    const bool baseHit = cacheBase && m_skin_base_pixels.size() == baseBytes &&
        !(m_skin_base_style != m_style) && m_skin_base_size == base_size &&
        m_skin_base_rect == backrc && m_skin_base_offset == offset &&
        m_skin_base_dpi == dpi && m_skin_base_revision == m_skin_revision &&
        m_skin_base_background == background && m_skin_base_rects == m_skin_rects;
    if (baseHit) {
      std::memcpy(pixels, m_skin_base_pixels.data(), baseBytes);
    } else {
      // Drop the previous size before allocating scaled images or a new base.
      std::vector<BYTE>().swap(m_skin_base_pixels);
      if (background)
        _HighlightText(memDC, backrc, m_style.back_color, m_style.shadow_color,
                     DPI_SCALE(m_style.round_corner_ex), BackType::BACKGROUND,
                     IsToRoundStruct(), m_style.border_color);
      GdiFlush();
      Gdiplus::Bitmap under(rcw.Width(), rcw.Height(), rcw.Width() * 4,
                            PixelFormat32bppPARGB, static_cast<BYTE*>(pixels));
      Gdiplus::Graphics graphics(&under);
      graphics.SetInterpolationMode(
          Gdiplus::InterpolationModeHighQualityBicubic);
      _DrawSkinImages(graphics, CPoint(0, 0), true);
      graphics.Flush(Gdiplus::FlushIntentionSync);
      const auto& frame = m_style.skin_frame_image.empty() ? m_style.background_image : m_style.skin_frame_image;
      // Failed frame loads must remain retryable; do not freeze an incomplete base.
      const bool frameReady = !background || frame.empty() || IS_FULLSCREENLAYOUT(m_style) ||
          (m_bg_image && m_bg_image_path == frame && !m_bg_image_stale);
      if (cacheBase && frameReady && _SkinResourceBytes() + baseBytes <= 64ULL * 1024 * 1024) {
        try {
          m_skin_base_pixels.assign(static_cast<BYTE*>(pixels), static_cast<BYTE*>(pixels) + baseBytes);
          m_skin_base_style = m_style; m_skin_base_size = base_size; m_skin_base_rect = backrc;
          m_skin_base_offset = offset; m_skin_base_dpi = dpi; m_skin_base_revision = m_skin_revision;
          m_skin_base_background = background; m_skin_base_rects = m_skin_rects;
          if (GetPropW(m_hWnd, L"Weasel.RenderProbe")) {
            auto generation = reinterpret_cast<ULONG_PTR>(GetPropW(m_hWnd, L"Weasel.StaticCacheGeneration"));
            SetPropW(m_hWnd, L"Weasel.StaticCacheGeneration", reinterpret_cast<HANDLE>(generation + 1));
          }
        } catch (const std::bad_alloc&) { std::vector<BYTE>().swap(m_skin_base_pixels); }
      }
    }
    // background and candidates back, hilite back drawing start
    if (!m_ctx.aux.str.empty()) {
      if (m_istorepos)
        auxrc.OffsetRect(0, m_offsety_aux);
      drawn |= _DrawPreeditBack(m_ctx.aux, memDC, auxrc);
    }
    if (!m_layout->IsInlinePreedit() && !m_ctx.preedit.str.empty()) {
      if (m_istorepos)
        preeditrc.OffsetRect(0, m_offsety_preedit);
      drawn |= _DrawPreeditBack(m_ctx.preedit, memDC, preeditrc);
    }
    if (m_candidateCount)
      drawn |= _DrawCandidates(memDC, true);
    _DrawAnnotationSeparators(memDC);
    // background and candidates back, hilite back drawing end

    // begin  texts drawing, if pRenderTarget failed, force to reinit
    // directwrite resources
    if (FAILED(pDWR->pRenderTarget->BindDC(memDC, &rcw))) {
      _InitFontRes(true);
      pDWR->pRenderTarget->BindDC(memDC, &rcw);
    }
    pDWR->pRenderTarget->BeginDraw();
    // draw auxiliary string
    if (!m_ctx.aux.str.empty())
      drawn |= _DrawPreedit(m_ctx.aux, memDC, auxrc);
    // draw preedit string
    if (!m_layout->IsInlinePreedit() && !m_ctx.preedit.str.empty())
      drawn |= _DrawPreedit(m_ctx.preedit, memDC, preeditrc);
    // draw candidates string
    if (m_candidateCount)
      drawn |= _DrawCandidates(memDC);
    if (FAILED(pDWR->pRenderTarget->EndDraw())) {
      _InitFontRes(true);
      Refresh();
    }
    // end texts drawing

    // status icon (I guess Metro IME stole my idea :)
    if (m_layout->ShouldDisplayStatusIcon()) {
      // decide if custom schema zhung icon to show
      LoadIconNecessary(m_current_zhung_icon, m_style.current_zhung_icon,
                        m_iconEnabled, IDI_ZH);
      LoadIconNecessary(m_current_ascii_icon, m_style.current_ascii_icon,
                        m_iconAlpha, IDI_EN);
      LoadIconNecessary(m_current_half_icon, m_style.current_half_icon,
                        m_iconHalf, IDI_HALF_SHAPE);
      LoadIconNecessary(m_current_full_icon, m_style.current_full_icon,
                        m_iconFull, IDI_FULL_SHAPE);
      CRect iconRect(m_layout->GetStatusIconRect());
      if (m_istorepos && !m_ctx.aux.str.empty())
        iconRect.OffsetRect(0, m_offsety_aux);
      else if (m_istorepos && !m_layout->IsInlinePreedit() &&
               !m_ctx.preedit.str.empty())
        iconRect.OffsetRect(0, m_offsety_preedit);

      CIcon& icon(
          m_status.disabled ? m_iconDisabled
          : m_status.ascii_mode
              ? m_iconAlpha
              : (m_status.type == SCHEMA
                     ? m_iconEnabled
                     : (m_status.full_shape ? m_iconFull : m_iconHalf)));
      memDC.DrawIconEx(iconRect.left, iconRect.top, icon, 0, 0);
      drawn = true;
    }
    /* Nothing drawn, hide candidate window */
    if (!drawn)
      ShowWindow(SW_HIDE);
  }
  _LayerUpdate(rcw, memDC);

  // clean objs
  ::SelectObject(memDC, oldBitmap);
  ::DeleteDC(memDC);
  ::DeleteObject(memBitmap);
}

// 由于某些软件并不依赖 WM_PAINT 消息来重绘，在消息循环中直接忽略掉了 WM_PAINT
// 消息， 导致 DoPaint() 永远不会被调用，这里手动调用 DoPaint() 强制重绘
void WeaselPanel::RedrawWindow() {
  HDC hdc = GetDC();
  DoPaint(hdc);
  ReleaseDC(hdc);
}

void WeaselPanel::_TraceSkinState(const char* reason) {
  _TraceSkinState(reason, {});
}

void WeaselPanel::_TraceSkinState(const char* reason, const std::string& detail) {
  try {
    auto root = SkinDataRoot() / L"render-logs";
    wchar_t overrideRoot[32768] = {};
    if (GetEnvironmentVariableW(L"RIME_RENDER_LOG_ROOT", overrideRoot, 32768)) root = overrideRoot;
    std::filesystem::create_directories(root);
    auto path = root / (L"render-" + std::to_wstring(GetCurrentProcessId()) + L".log");
    if (std::filesystem::exists(path) && std::filesystem::file_size(path) > 128 * 1024) {
      std::filesystem::copy_file(path, path.wstring() + L".previous", std::filesystem::copy_options::overwrite_existing);
      std::ofstream reset(path, std::ios::trunc);
    }
    std::ofstream stream(path, std::ios::app);
    stream << GetTickCount64() << ' ' << reason << " requested=" << m_style.skin_images.size()
           << " decoded=" << m_skin_bitmaps.size() << " canvas=" << m_skin_size.cx << ',' << m_skin_size.cy
           << " origin=" << m_skin_origin.x << ',' << m_skin_origin.y << " dpi=" << dpi
           << " thread=" << GetCurrentThreadId() << " gdip_startup=" << m_gdiplus_startup_status;
    if (!detail.empty()) stream << ' ' << detail;
    for (const auto& image : m_style.skin_images) {
      const auto name = std::filesystem::path(image.path).filename().wstring();
      if (name.size() > 64 && name[64] == L'.' &&
          std::all_of(name.begin(), name.begin() + 64, [](wchar_t c) {
            return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
          })) {
        stream << " asset=" << std::string(name.begin(), name.begin() + 64);
      }
    }
    stream << '\n';
  } catch (const std::exception&) { }
}

void WeaselPanel::_DrawAnnotationSeparators(CDCHandle dc) {
  if (m_layout->annotation_separators.empty()) return;
  GdiFlush(); // Complete queued GDI backgrounds before writing their DIB pixels directly.
  DIBSECTION section = {};
  if (!GetObjectW(GetCurrentObject(dc, OBJ_BITMAP), sizeof(section), &section) || !section.dsBm.bmBits) return;
  Gdiplus::Bitmap bitmap(rcw.Width(), rcw.Height(), section.dsBm.bmWidthBytes,
                         PixelFormat32bppPARGB, static_cast<BYTE*>(section.dsBm.bmBits));
  Gdiplus::Graphics graphics(&bitmap);
  const auto color = Gdiplus::Color(GDPCOLOR_FROM_COLORREF(m_style.annotation_separator_color));
  for (const auto& rect : m_layout->annotation_separators) {
    if (rect.IsRectEmpty()) continue;
    Gdiplus::Rect target(rect.left, rect.top, rect.Width(), rect.Height());
    if (m_style.annotation_separator_fade) {
      Gdiplus::Color transparent(0, color.GetR(), color.GetG(), color.GetB());
      Gdiplus::Color colors[] = {transparent, color, color, transparent};
      Gdiplus::REAL positions[] = {0.f, .2f, .8f, 1.f};
      Gdiplus::LinearGradientBrush brush(target, transparent, transparent, Gdiplus::LinearGradientModeHorizontal);
      brush.SetInterpolationColors(colors, positions, 4); graphics.FillRectangle(&brush, target);
    } else {
      Gdiplus::SolidBrush brush(color); graphics.FillRectangle(&brush, target);
    }
  }
  graphics.Flush(Gdiplus::FlushIntentionSync);
}

uint64_t WeaselPanel::_SkinResourceBytes() const {
  uint64_t bytes = m_skin_base_pixels.size();
  for (const auto& bitmap : m_skin_bitmaps)
    if (bitmap) bytes += uint64_t(bitmap->GetWidth()) * bitmap->GetHeight() * 4;
  for (const auto& bitmap : m_skin_scaled_bitmaps)
    if (bitmap) bytes += uint64_t(bitmap->GetWidth()) * bitmap->GetHeight() * 4;
  if (m_bg_image) bytes += uint64_t(m_bg_image->GetWidth()) * m_bg_image->GetHeight() * 4;
  return bytes;
}

void WeaselPanel::_DrawSkinImages(Gdiplus::Graphics& graphics,
                                  CPoint origin,
                                  bool below) {
  bool cache = !GetPropW(m_hWnd, L"Weasel.DisableImageScaleCache"); // Isolated regression/benchmark control.
  if (cache) {
    try {
      m_skin_scaled_bitmaps.resize(m_skin_bitmaps.size());
      m_skin_scaled_sizes.resize(m_skin_bitmaps.size(), CSize(0, 0));
    } catch (const std::bad_alloc&) { cache = false; }
  }
  for (size_t i = 0; i < m_skin_rects.size(); ++i) {
    if (!m_skin_bitmaps[i] || (m_skin_config[i].z < 0) != below)
      continue;
    CRect target = m_skin_rects[i];
    target.OffsetRect(origin);
    auto* image = m_skin_bitmaps[i].get();
    const float scale = (std::min)((float)target.Width() / image->GetWidth(),
                                   (float)target.Height() / image->GetHeight());
    const int width = (int)(image->GetWidth() * scale);
    const int height = (int)(image->GetHeight() * scale);
    if (cache && m_skin_scaled_sizes[i] != CSize(width, height)) {
      m_skin_scaled_bitmaps[i].reset();
      m_skin_scaled_sizes[i] = CSize(width, height);
      // Original pixels plus optional scaled copies stay inside the existing
      // decoded-resource budget. Oversized/failed copies use the valid original.
      const uint64_t bytes = _SkinResourceBytes();
      if (width > 0 && height > 0 && bytes + uint64_t(width) * height * 4 <= 64ULL * 1024 * 1024)
        m_skin_scaled_bitmaps[i] = ScaleSkinBitmap(*image, width, height,
            below ? Gdiplus::PixelOffsetModeDefault : Gdiplus::PixelOffsetModeHighQuality);
    }
    const bool scaled = cache && m_skin_scaled_bitmaps[i];
    const auto interpolation = graphics.GetInterpolationMode();
    const auto offset = graphics.GetPixelOffsetMode();
    if (scaled) {
      graphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
      graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeNone);
    }
    graphics.DrawImage(scaled ? m_skin_scaled_bitmaps[i].get() : image,
        target.left + (target.Width() - width) / 2, target.bottom - height, width, height);
    if (scaled) { graphics.SetInterpolationMode(interpolation); graphics.SetPixelOffsetMode(offset); }
  }
}

void WeaselPanel::_LayerUpdate(const CRect& rc, CDCHandle dc) {
  HDC ScreenDC = ::GetDC(NULL);
  CRect rect;
  GetWindowRect(&rect);
  POINT WindowPosAtScreen = {rect.left, rect.top};
  POINT PointOriginal = {0, 0};
  SIZE sz = {m_skin_size.cx, m_skin_size.cy};

  HDC skinDC = NULL;
  HBITMAP skinBitmap = NULL;
  HGDIOBJ oldBitmap = NULL;
  if (!m_skin_rects.empty()) {
    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = sz.cx;
    info.bmiHeader.biHeight = -sz.cy;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = NULL;
    skinBitmap =
        CreateDIBSection(ScreenDC, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
    if (!skinBitmap) {
      ReleaseDC(ScreenDC);
      return;
    }
    memset(pixels, 0, (size_t)sz.cx * sz.cy * 4);
    skinDC = CreateCompatibleDC(ScreenDC);
    oldBitmap = SelectObject(skinDC, skinBitmap);
    {
      Gdiplus::Bitmap under(sz.cx, sz.cy, sz.cx * 4, PixelFormat32bppPARGB,
                            static_cast<BYTE*>(pixels));
      Gdiplus::Graphics graphics(&under);
      graphics.SetInterpolationMode(
          Gdiplus::InterpolationModeHighQualityBicubic);
      _DrawSkinImages(graphics, m_skin_origin, true);
      graphics.Flush(Gdiplus::FlushIntentionSync);
    }
    // Copy the candidate pixels, including premultiplied alpha, unchanged.
    BitBlt(skinDC, m_skin_origin.x, m_skin_origin.y, rc.Width(), rc.Height(),
           dc, 0, 0, SRCCOPY);
    GdiFlush();
    Gdiplus::Bitmap canvas(sz.cx, sz.cy, sz.cx * 4, PixelFormat32bppPARGB,
                           static_cast<BYTE*>(pixels));
    Gdiplus::Graphics graphics(&canvas);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    _DrawSkinImages(graphics, m_skin_origin, false);
    graphics.Flush(Gdiplus::FlushIntentionSync);
    dc = skinDC;
  }

  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 0XFF, AC_SRC_ALPHA};
  if (UpdateLayeredWindow(m_hWnd, ScreenDC, &WindowPosAtScreen, &sz, dc,
                          &PointOriginal, RGB(0, 0, 0), &bf, ULW_ALPHA)) {
    // Opt-in diagnostic probe: count successful frame submissions, not WM_PAINT.
    // RedrawWindow deliberately paints directly for applications which discard WM_PAINT.
    if (::GetPropW(m_hWnd, L"Weasel.RenderProbe")) {
      auto generation = reinterpret_cast<ULONG_PTR>(::GetPropW(m_hWnd, L"Weasel.RenderGeneration"));
      ::SetPropW(m_hWnd, L"Weasel.RenderGeneration", reinterpret_cast<HANDLE>(generation + 1));
      DIBSECTION section = {};
      if (::GetObjectW(::GetCurrentObject(dc, OBJ_BITMAP), sizeof(section), &section) && section.dsBm.bmBits) {
        auto pixels = static_cast<const BYTE*>(section.dsBm.bmBits);
        auto length = static_cast<size_t>(section.dsBm.bmWidthBytes) * abs(section.dsBm.bmHeight);
        ULONG_PTR hash = 2166136261UL;
        for (size_t i = 0; i < length; ++i) hash = (hash ^ pixels[i]) * 16777619UL;
        ::SetPropW(m_hWnd, L"Weasel.RenderHash", reinterpret_cast<HANDLE>(hash));
      }
    }
  } else _TraceSkinState("layer_update_failed");
  if (skinDC) {
    SelectObject(skinDC, oldBitmap);
    DeleteDC(skinDC);
    DeleteObject(skinBitmap);
  }
  ReleaseDC(ScreenDC);
}

LRESULT WeaselPanel::OnCreate(UINT uMsg,
                              WPARAM wParam,
                              LPARAM lParam,
                              BOOL& bHandled) {
  m_mouse_entry = false;
  m_hoverIndex = -1;
  Refresh();
  return TRUE;
}

LRESULT WeaselPanel::OnDestroy(UINT uMsg,
                               WPARAM wParam,
                               LPARAM lParam,
                               BOOL& bHandled) {
  m_hoverIndex = -1;
  _ReleaseSkinImages();
  m_lastMousePos = {-1, -1};
  m_hasMousePos = false;
  m_sticky = false;
  m_has_caret = false;
  delete m_layout;
  m_layout = NULL;
  delete m_bg_image;
  m_bg_image = NULL;
  m_bg_image_path.clear();  // 图已释放，路径缓存同步失效，否则新窗口永不重载
  return 0;
}

LRESULT WeaselPanel::OnDpiChanged(UINT uMsg,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  BOOL& bHandled) {
  Refresh();
  return LRESULT();
}

void WeaselPanel::MoveTo(RECT const& rc) {
  if (ShouldUpdateCandidateAnchor(
      {m_caret_rect.left, m_caret_rect.top, m_caret_rect.right, m_caret_rect.bottom},
      {rc.left, rc.top, rc.right, rc.bottom}, m_has_caret)) {
    m_caret_rect = rc;
    m_has_caret = true;
  }
  if (!m_layout)
    return;  // avoid handling nullptr in _RepositionWindow
  m_redraw_by_monitor_change = false;
  // if ascii_tip_follow_cursor set, move tip icon to mouse cursor
  if (m_style.ascii_tip_follow_cursor && m_ctx.empty() &&
      (!m_status.composing) && m_layout->ShouldDisplayStatusIcon()) {
    // ascii icon follow cursor
    POINT p;
    ::GetCursorPos(&p);
    RECT irc{p.x - STATUS_ICON_SIZE, p.y - STATUS_ICON_SIZE, p.x, p.y};
    m_inputPos = irc;
    _RepositionWindow(true);
    RedrawWindow();
  } else if (!(rc.left == m_inputPos.left && rc.bottom != m_inputPos.bottom &&
               abs(rc.bottom - m_inputPos.bottom) < 6) ||
             m_layout->ShouldDisplayStatusIcon()) {
    // in some apps like word 2021, with inline_preedit set,
    // bottom of rc would flicker 1 px or 2, make the candidate flickering
    m_inputPos = rc;
    m_inputPos.OffsetRect(0, 6);
    // buffer current m_istorepos status
    bool m_istorepos_buf = m_istorepos;
    // with parameter to avoid vertical flicker
    _RepositionWindow(true);
    // m_istorepos status changed by _RepositionWindow, or tips to show,
    // redrawing is required
    if (m_istorepos != m_istorepos_buf || !m_ctx.aux.empty() ||
        m_layout->ShouldDisplayStatusIcon() || m_redraw_by_monitor_change)
      RedrawWindow();
  }
}

void WeaselPanel::_RepositionWindow(const bool& adj) {
  RECT rcWorkArea;
  memset(&rcWorkArea, 0, sizeof(rcWorkArea));
  HMONITOR hMonitor = MonitorFromRect(m_inputPos, MONITOR_DEFAULTTONEAREST);
  if (hMonitor) {
    MONITORINFO info;
    info.cbSize = sizeof(MONITORINFO);
    if (GetMonitorInfo(hMonitor, &info)) {
      rcWorkArea = info.rcWork;
    }
    if (hMonitor != m_hMonitor) {
      m_hMonitor = hMonitor;
      m_redraw_by_monitor_change = true;
    }
  }
  RECT rcWindow;
  GetWindowRect(&rcWindow);
  int width = (rcWindow.right - rcWindow.left);
  int height = (rcWindow.bottom - rcWindow.top);
  if (m_style.layout_type == UIStyle::LAYOUT_HORIZONTAL &&
      m_status.composing && m_candidateCount && !hide_candidates &&
      rcWorkArea.right > rcWorkArea.left && rcWorkArea.bottom > rcWorkArea.top) {
    if (!m_has_caret) return;
    CRect frame = m_layout->GetContentRect(); frame.OffsetRect(m_skin_origin);
    std::vector<SkinRect> images;
    for (auto image : m_skin_rects) {
      image.OffsetRect(m_skin_origin);
      images.push_back({image.left, image.top, image.right, image.bottom});
    }
    auto placed = PlaceCandidateWindow(
        {m_caret_rect.left, m_caret_rect.top, m_caret_rect.right, m_caret_rect.bottom},
        {rcWorkArea.left, rcWorkArea.top, rcWorkArea.right, rcWorkArea.bottom},
        width, height, {frame.left, frame.top, frame.right, frame.bottom}, images,
        DPI_SCALE(m_placement.offset_x), DPI_SCALE(m_placement.gap),
        m_placement.preference, m_placement.avoid_decorations);
    m_istorepos = false;
    SetWindowPos(HWND_TOPMOST, placed.x, placed.y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
    return;
  }
  // keep panel visible
  rcWorkArea.right -= width;
  rcWorkArea.bottom -= height;
  int x = m_inputPos.left - m_skin_origin.x;
  int y = m_inputPos.bottom - m_skin_origin.y;
  if (DPI_SCALE(m_style.shadow_radius)) {
    x -= (DPI_SCALE(m_style.shadow_offset_x) >= 0 ||
          COLORTRANSPARENT(m_style.shadow_color))
             ? m_layout->offsetX
             : (m_layout->offsetX / 2);
    if (adj)
      y -= (DPI_SCALE(m_style.shadow_offset_y) > 0 ||
            COLORTRANSPARENT(m_style.shadow_color))
               ? m_layout->offsetY
               : (m_layout->offsetY / 2);
  }
  // for vertical text layout, flow right to left, make window left side
  if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT &&
      !m_style.vertical_text_left_to_right) {
    x += m_layout->offsetX - width;
    if (DPI_SCALE(m_style.shadow_offset_x) < 0)
      x += m_layout->offsetX;
  }
  if (adj)
    m_istorepos = false;
  if (x > rcWorkArea.right)
    x = rcWorkArea.right;  // over workarea right
  if (x < rcWorkArea.left)
    x = rcWorkArea.left;  // over workarea left
  // show panel above the input focus if we're around the bottom
  if (y > rcWorkArea.bottom || m_sticky) {
    if (!m_sticky)
      m_sticky = true;
    y = m_inputPos.top - height - 6;  // over workarea bottom
    if (DPI_SCALE(m_style.shadow_radius) &&
        DPI_SCALE(m_style.shadow_offset_y) > 0)
      y -= DPI_SCALE(m_style.shadow_offset_y);
    m_istorepos = (m_style.vertical_auto_reverse &&
                   m_style.layout_type == UIStyle::LAYOUT_VERTICAL);
    if (DPI_SCALE(m_style.shadow_radius) > 0)
      y += (DPI_SCALE(m_style.shadow_offset_y) < 0 ||
            COLORTRANSPARENT(m_style.shadow_color))
               ? m_layout->offsetY
               : (m_layout->offsetY / 2);
  }
  if (y < rcWorkArea.top)
    y = rcWorkArea.top;  // over workarea top
  // memorize adjusted position (to avoid window bouncing on height change)
  m_inputPos.bottom = y + m_skin_origin.y;
  SetWindowPos(HWND_TOPMOST, x, y, 0, 0,
               SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
}

void WeaselPanel::ReloadPlacement() {
  try {
    if (GetPropW(m_hWnd, L"Weasel.RenderProbe")) {
      auto count = reinterpret_cast<ULONG_PTR>(GetPropW(m_hWnd, L"Weasel.PlacementReads"));
      SetPropW(m_hWnd, L"Weasel.PlacementReads", reinterpret_cast<HANDLE>(count + 1));
    }
    std::string error;
    const auto path = m_placement_path.empty() ? SkinDataRoot() / L"display.json" : m_placement_path;
    if (!LoadCandidatePlacement(path, m_placement, error))
      _TraceSkinState("placement_invalid_preserving_previous");
  } catch (const std::exception&) {
    _TraceSkinState("placement_invalid_preserving_previous");
  }
  if (m_layout && IsWindow()) _RepositionWindow();
}

void WeaselPanel::_TextOut(const CRect& rc,
                           const std::wstring& psz,
                           const size_t& cch,
                           const int& inColor,
                           IDWriteTextFormat1* const pTextFormat) {
  if (pTextFormat == NULL || rc.IsRectEmpty())
    return;
  float r = (float)(GetRValue(inColor)) / 255.0f;
  float g = (float)(GetGValue(inColor)) / 255.0f;
  float b = (float)(GetBValue(inColor)) / 255.0f;
  float alpha = (float)((inColor >> 24) & 255) / 255.0f;
  HRESULT hr = S_OK;
  if (pDWR->pBrush == NULL) {
    HR(pDWR->CreateBrush(D2D1::ColorF(r, g, b, alpha)));
  } else
    pDWR->SetBrushColor(D2D1::ColorF(r, g, b, alpha));

  HR(pDWR->CreateTextLayout(psz.c_str(), (int)cch, pTextFormat,
                            (float)rc.Width(), (float)rc.Height()));
  if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT) {
    DWRITE_FLOW_DIRECTION flow = m_style.vertical_text_left_to_right
                                     ? DWRITE_FLOW_DIRECTION_LEFT_TO_RIGHT
                                     : DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT;
    HR(pDWR->SetLayoutReadingDirection(DWRITE_READING_DIRECTION_TOP_TO_BOTTOM));
    HR(pDWR->SetLayoutFlowDirection(flow));
  }

  // offsetx for font glyph over left
  float offsetx = (float)rc.left;
  float offsety = (float)rc.top;
  // prepare for space when first character overhanged
  DWRITE_OVERHANG_METRICS omt;
  HR(pDWR->GetLayoutOverhangMetrics(&omt));
  if (m_style.layout_type != UIStyle::LAYOUT_VERTICAL_TEXT && omt.left > 0)
    offsetx += omt.left;
  if (m_style.layout_type == UIStyle::LAYOUT_VERTICAL_TEXT && omt.top > 0)
    offsety += omt.top;

  if (pDWR->pTextLayout != NULL) {
    pDWR->DrawTextLayoutAt({offsetx, offsety});
#if 0
    D2D1_RECT_F rectf =  D2D1::RectF(offsetx, offsety, offsetx + rc.Width(), offsety + rc.Height());
    pDWR->DrawRect(&rectf);
#endif
  }
}
