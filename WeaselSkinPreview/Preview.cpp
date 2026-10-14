#include <atlbase.h>
#include <atlwin.h>
#include <wtl/atlapp.h>
#include <wtl/atlgdi.h>
#include <wtl/atlmisc.h>
#include <WeaselUI.h>
#include <WeaselSkin.h>
#include "../WeaselUI/HorizontalLayout.h"
#include <shellapi.h>
#include <ShellScalingApi.h>
#pragma comment(lib, "Shcore.lib")
#include <fstream>

CAppModule _Module;
static weasel::UI ui;
static weasel::Context context;
static weasel::Status status;
static bool long_text = false;
static std::filesystem::path manifest;
static std::string variant;
static std::filesystem::path overrides;
static bool LayoutTests(HWND window) {
  CClientDC dc(window);
  weasel::Context sample; weasel::Status state; state.composing = true;
  for (const auto* word : {L"守岸人", L"很长的一条候选内容用来检查边界", L"你好"}) {
    weasel::Text text, label, comment; text.str = word; label.str = L"1";
    comment.str = sample.cinfo.candies.empty() ? L"shǒu àn rén" : L"";
    sample.cinfo.candies.push_back(text); sample.cinfo.labels.push_back(label);
    sample.cinfo.comments.push_back(comment);
  }
  for (UINT dpi : {96u, 120u, 144u, 192u}) for (int position : {0, 1, 2}) {
    auto style = ui.style(); style.annotation_position = position;
    auto resources = std::make_shared<weasel::DirectWriteResources>(style, dpi);
    weasel::HorizontalLayout layout(style, sample, state, resources);
    layout.DoLayout(dc, resources); auto size = layout.GetContentSize();
    for (int i = 0; i < 3; ++i) {
      auto candidate = layout.GetCandidateRect(i), text = layout.GetCandidateTextRect(i);
      if (candidate.left > text.left || candidate.top > text.top || candidate.right < text.right || candidate.bottom < text.bottom ||
          candidate.left < 0 || candidate.top < 0 || candidate.right > size.cx || candidate.bottom > size.cy) return false;
      auto comment = layout.GetCandidateCommentRect(i);
      if (position == 2 && comment.Width()) return false;
      if (position == 1 && i == 0 && comment.bottom >= text.top) return false;
      if (comment.Width() && (candidate.left > comment.left || candidate.right < comment.right || candidate.top > comment.top || candidate.bottom < comment.bottom)) return false;
    }
  }
  return true;
}
static void UpdatePreview(HWND window) {
  context.preedit.str = long_text ? L"wo xiang zai zhe li chang shi yi xia shu ru" : L"shou'an'ren";
  context.cinfo.candies.clear(); context.cinfo.labels.clear(); context.cinfo.comments.clear();
  const wchar_t* shortWords[] = {L"守岸人", L"受案人", L"受案", L"收案", L"寿安"};
  const wchar_t* tones[] = {L"shǒu àn rén", L"shòu àn rén", L"shòu àn", L"shōu àn", L"shòu ān"};
  for (int i = 0; i < 5; ++i) {
    weasel::Text text, label, comment;
    text.str = long_text && i == 0 ? L"我想在这里尝试一下输入" : shortWords[i];
    label.str = std::to_wstring(i + 1); comment.str = tones[i];
    context.cinfo.candies.push_back(text); context.cinfo.labels.push_back(label); context.cinfo.comments.push_back(comment);
  }
  RECT rect; GetWindowRect(window, &rect);
  RECT cursor{rect.left + 70, rect.top + 260, rect.left + 72, rect.top + 280};
  ui.Update(context, status); ui.UpdateInputPosition(cursor); ui.Show();
}
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
  switch (message) {
    case WM_CREATE: SetTimer(hwnd, 1, 1500, nullptr); break;
    case WM_TIMER: long_text = !long_text; UpdatePreview(hwnd); break;
    case WM_MOVE: if (ui.pdwr()) UpdatePreview(hwnd); break;
    case WM_KEYDOWN:
      if (w == '1' || w == '2' || w == '3') { ui.style().annotation_position = w - '1'; ui.Refresh(); }
      if (w == VK_ESCAPE) DestroyWindow(hwnd);
      break;
    case WM_PAINT: {
      PAINTSTRUCT paint; HDC dc = BeginPaint(hwnd, &paint);
      SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(40, 60, 80));
      RECT text{30, 30, 780, 150};
      DrawTextW(dc, L"原生皮肤预览 · 不连接输入法、不读取词库\n候选自动切换长短文本；拖动窗口测试屏幕边缘与跨屏\n按 1 上方注音 / 2 旁边注音 / 3 隐藏；Esc 关闭", -1, &text, DT_LEFT);
      EndPaint(hwnd, &paint); return 0;
    }
    case WM_DESTROY: ui.Destroy(true); PostQuitMessage(0); return 0;
  }
  return DefWindowProc(hwnd, message, w, l);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); _Module.Init(nullptr, instance);
  int count = 0; LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
  bool layout_test = count == 2 && !wcscmp(args[1], L"--layout-test");
  for (int i = 1; i + 1 < count; i += 2) {
    if (!wcscmp(args[i], L"--manifest")) manifest = args[i + 1];
    if (!wcscmp(args[i], L"--variant")) variant = wtou8(args[i + 1]);
    if (!wcscmp(args[i], L"--style-overrides")) overrides = args[i + 1];
  }
  LocalFree(args);
  WNDCLASSW cls{}; cls.hInstance = instance; cls.lpfnWndProc = WindowProc;
  cls.lpszClassName = L"ShorekeeperNativePreview"; cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
  cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClassW(&cls);
  HWND window = CreateWindowW(cls.lpszClassName, L"守岸人 · 原生皮肤预览", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1000, 650, nullptr, nullptr, instance, nullptr);
  auto& style = ui.style();
  style.font_face = style.label_font_face = style.comment_font_face = L"Microsoft YaHei UI";
  style.font_point = style.label_font_point = 16; style.comment_font_point = 11;
  style.layout_type = weasel::UIStyle::LAYOUT_HORIZONTAL; style.inline_preedit = false;
  style.back_color = 0xfffffbf5; style.candidate_text_color = style.text_color = 0xff432f1c;
  style.label_text_color = style.comment_text_color = 0xff997f61;
  style.hilited_candidate_text_color = style.hilited_label_text_color = 0xffc87b20;
  style.hilited_comment_text_color = 0xff997f61; style.hilited_candidate_back_color = 0xffffeedd;
  style.border_color = 0xffe9a94d; style.shadow_color = 0x30000000;
  style.hilite_spacing = 5; style.hilite_padding_x = 3; style.hilite_padding_y = 3; style.max_width = 800;
  ui.Create(window);
  if (layout_test) {
    bool passed = LayoutTests(window); DestroyWindow(window); _Module.Term(); CoUninitialize(); return passed ? 0 : 4;
  }
  std::string error;
  if (!weasel::LoadSkinManifest(manifest, variant, style, error, overrides)) {
    MessageBoxW(window, u8tow(error).c_str(), L"皮肤加载失败", MB_ICONERROR);
    DestroyWindow(window); _Module.Term(); CoUninitialize(); return 1;
  }
  status.composing = true; status.schema_id = L"preview"; status.schema_name = L"隔离预览";
  ui.SetUICallBack([window](size_t* selected, size_t* highlighted, bool*, bool*) {
    if (selected) SetWindowTextW(window, (L"已点击候选 " + std::to_wstring(*selected + 1)).c_str());
    if (highlighted) { context.cinfo.highlighted = (int)*highlighted; UpdatePreview(window); }
  });
  style.hover_type = weasel::UIStyle::HILITE;
  ShowWindow(window, show); UpdatePreview(window);
  MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  _Module.Term(); CoUninitialize(); return 0;
}
