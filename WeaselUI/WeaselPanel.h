#pragma once
#include <WeaselIPCData.h>
#include <WeaselUI.h>
#include <filesystem>
#include "StandardLayout.h"
#include "Layout.h"
#include "GdiplusBlur.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using namespace weasel;

typedef CWinTraits<WS_POPUP | WS_CLIPSIBLINGS | WS_DISABLED,
                   WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
                       WS_EX_LAYERED>
    CWeaselPanelTraits;

enum class BackType {
  TEXT = 0,
  CAND = 1,
  BACKGROUND = 2  // background
};

class WeaselPanel
    : public CWindowImpl<WeaselPanel, CWindow, CWeaselPanelTraits>,
      CDoubleBufferImpl<WeaselPanel> {
 public:
  BEGIN_MSG_MAP(WeaselPanel)
  MESSAGE_HANDLER(WM_CREATE, OnCreate)
  MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
  MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
  MESSAGE_HANDLER(WM_MOUSEACTIVATE, OnMouseActivate)
  MESSAGE_HANDLER(WM_LBUTTONUP, OnLeftClickedUp)
  MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLeftClickedDown)
  MESSAGE_HANDLER(WM_MOUSEWHEEL, OnMouseWheel)
  MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
  MESSAGE_HANDLER(WM_MOUSELEAVE, OnMouseLeave)
  CHAIN_MSG_MAP(CDoubleBufferImpl<WeaselPanel>)
  END_MSG_MAP()

  LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnDpiChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnMouseActivate(UINT uMsg,
                          WPARAM wParam,
                          LPARAM lParam,
                          BOOL& bHandled);
  LRESULT OnLeftClickedUp(UINT uMsg,
                          WPARAM wParam,
                          LPARAM lParam,
                          BOOL& bHandled);
  LRESULT OnLeftClickedDown(UINT uMsg,
                            WPARAM wParam,
                            LPARAM lParam,
                            BOOL& bHandled);
  LRESULT OnMouseWheel(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnMouseLeave(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

  WeaselPanel(weasel::UI& ui);
  ~WeaselPanel();

  void MoveTo(RECT const& rc);
  void Refresh();
  void DoPaint(CDCHandle dc);
  bool GetIsReposition() { return m_istorepos; }
  void RedrawWindow();

  static VOID CALLBACK OnTimer(_In_ HWND hwnd,
                               _In_ UINT uMsg,
                               _In_ UINT_PTR idEvent,
                               _In_ DWORD dwTime);
  static const int AUTOREV_TIMER = 20240315;
  static UINT_PTR ptimer;

 private:
  template <typename T>
  int DPI_SCALE(T t) {
    return (int)(t * dpiScaleLayout);
  }
  void _InitFontRes(bool forced = false);
  void _CaptureRect(CRect& rect);
  bool m_mouse_entry = false;
  void _CreateLayout();
  void _ResizeWindow();
  void _RepositionWindow(const bool& adj = false);
  bool _DrawPreedit(const Text& text, CDCHandle dc, const CRect& rc);
  bool _DrawPreeditBack(const Text& text, CDCHandle dc, const CRect& rc);
  bool _DrawCandidates(CDCHandle& dc, bool back = false);
  void _HighlightText(CDCHandle& dc,
                      const CRect& rc,
                      const COLORREF& color,
                      const COLORREF& shadowColor,
                      const int& radius,
                      const BackType& type,
                      const IsToRoundStruct& rd,
                      const COLORREF& bordercolor);
  void _TextOut(const CRect& rc,
                const std::wstring& psz,
                const size_t& cch,
                const int& inColor,
                IDWriteTextFormat1* const pTextFormat = NULL);

  void _LayerUpdate(const CRect& rc, CDCHandle dc);
  void _UpdateSkinGeometry();
  void _ReleaseSkinImages();
  void _DrawSkinImages(Gdiplus::Graphics& graphics, CPoint origin, bool below);
  uint64_t _SkinResourceBytes() const;
  void _DrawAnnotationSeparators(CDCHandle dc);
  void _TraceSkinState(const char* reason);
  void _TraceSkinState(const char* reason, const std::string& detail);
  CPoint m_skin_origin = CPoint(0, 0);
  CSize m_skin_size = CSize(0, 0);
  std::vector<SkinImage> m_skin_config;
  std::vector<std::unique_ptr<Gdiplus::Bitmap>> m_skin_bitmaps;
  std::vector<std::unique_ptr<Gdiplus::Bitmap>> m_skin_scaled_bitmaps;
  std::vector<CSize> m_skin_scaled_sizes;
  std::vector<BYTE> m_skin_base_pixels;
  UIStyle m_skin_base_style;
  CSize m_skin_base_size = CSize(0, 0);
  CRect m_skin_base_rect;
  std::vector<CRect> m_skin_base_rects;
  CSize m_skin_base_offset = CSize(0, 0);
  size_t m_skin_base_revision = 0;
  UINT m_skin_base_dpi = 0;
  bool m_skin_base_background = false;
  std::vector<CRect> m_skin_rects;
  ULONGLONG m_skin_retry_after = 0;
  bool m_skin_load_failed = false;
  size_t m_skin_revision = 0;
  std::vector<std::filesystem::file_time_type> m_skin_stamps;

  weasel::Layout* m_layout;
  // 候选窗背景图：按路径缓存。本类每个输入线程一个实例，
  // 因此只能做成成员，不能写成函数内 static
  Gdiplus::Bitmap* m_bg_image;
  std::wstring m_bg_image_path;
  std::wstring m_skin_frame_path;
  std::filesystem::file_time_type m_skin_frame_stamp{};
  bool m_bg_image_stale = false;
  // 按 background_fill_mode 把底图画进 rc（内部处理 stretch/tile/center/fit）
  void _DrawBackgroundImage(Gdiplus::Graphics& g,
                            const CRect& rc,
                            Gdiplus::GraphicsPath* clip_path);
  weasel::Context& m_ctx;
  weasel::Context& m_octx;
  weasel::Status& m_status;
  weasel::UIStyle& m_style;
  weasel::UIStyle& m_ostyle;

  CRect m_inputPos;
  int m_offsetys[MAX_CANDIDATES_COUNT];  // offset y for candidates when
                                         // vertical layout over bottom
  int m_offsety_preedit;
  int m_offsety_aux;
  bool m_istorepos;

  CIcon m_iconDisabled;
  CIcon m_iconEnabled;
  CIcon m_iconAlpha;
  CIcon m_iconFull;
  CIcon m_iconHalf;
  std::wstring m_current_zhung_icon;
  std::wstring m_current_ascii_icon;
  std::wstring m_current_half_icon;
  std::wstring m_current_full_icon;
  // for gdiplus drawings
  Gdiplus::GdiplusStartupInput _m_gdiplusStartupInput;
  ULONG_PTR _m_gdiplusToken;
  Gdiplus::Status m_gdiplus_startup_status = Gdiplus::Ok;

  UINT dpi;

  CRect rcw;
  BYTE m_candidateCount;

  bool hide_candidates = true;
  bool m_sticky;
  // for multi font_face & font_point
  PDWR pDWR;
  std::function<void(size_t* const, size_t* const, bool* const, bool* const)>&
      _UICallback;
  float bar_scale_ = 1.0;
  float dpiScaleLayout = 1.0f;
  int m_hoverIndex = -1;
  HMONITOR m_hMonitor = NULL;
  bool m_redraw_by_monitor_change = false;
};
