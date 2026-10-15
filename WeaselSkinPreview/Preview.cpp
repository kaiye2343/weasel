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
#include <stdexcept>

CAppModule _Module;
static weasel::UI ui;
static weasel::Context context;
static weasel::Status status;
static bool long_text = false;
static std::filesystem::path manifest;
static std::string variant;
static std::filesystem::path overrides;
static std::filesystem::path display_preferences;
static int smoke_frames = 0;
static void PumpPreview() { MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } }
static bool RedrawTests(HWND owner) {
  std::ofstream report("native-redraw-test.log");
  KillTimer(owner, 1); // Keep synthetic data stable while testing appearance-only changes.
  ShowWindow(owner, SW_SHOWNOACTIVATE); ui.Show(); PumpPreview();
  HWND panel = ui.GetCandidateWindow(); if (!panel) { report << "Candidate window unavailable"; return false; }
  SetPropW(panel, L"Weasel.RenderProbe", reinterpret_cast<HANDLE>(1));
  auto frames = [panel]() { return reinterpret_cast<ULONG_PTR>(GetPropW(panel, L"Weasel.RenderGeneration")); };
  ui.ctx().preedit.str += L" "; ui.Refresh(); PumpPreview();
  report << "context-change frames=" << frames() << '\n';
  if (!frames()) { RemovePropW(panel, L"Weasel.RenderProbe"); return false; }
  auto beforeFrames = frames();
  ui.style().font_point += 1; ui.Refresh(); PumpPreview();
  bool passed = frames() > beforeFrames;
  report << "style-only frames=" << frames() - beforeFrames << '\n';
  beforeFrames = frames(); ui.Hide(); ui.Show(); PumpPreview(); passed = passed && frames() > beforeFrames;
  report << "reshow frames=" << frames() - beforeFrames << '\n';
  if (!ui.style().skin_images.empty()) {
    RECT before, after; GetWindowRect(panel, &before);
    HANDLE beforeHash = GetPropW(panel, L"Weasel.RenderHash");
    auto images = ui.style().skin_images;
    ui.style().skin_images[0].path += L".missing"; ui.Refresh(); PumpPreview();
    GetWindowRect(panel, &after);
    passed = passed && EqualRect(&before, &after);
    report << "resource rollback keeps canvas=" << (EqualRect(&before, &after) != FALSE) << '\n';
    bool samePixels = beforeHash && beforeHash == GetPropW(panel, L"Weasel.RenderHash");
    passed = passed && samePixels;
    report << "resource rollback keeps pixels=" << samePixels << '\n';
    ui.style().skin_images = images; ui.Refresh(); PumpPreview();
  }
  beforeFrames = frames();
  for (int i = 0; i < 30; ++i) { ui.style().annotation_position = (ui.style().annotation_position + 1) % 3; ui.Refresh(); PumpPreview(); }
  passed = passed && frames() - beforeFrames >= 30;
  report << "transition frames=" << frames() - beforeFrames << " passed=" << passed;
  ui.style().annotation_position = 2; ui.Refresh(); PumpPreview();
  HANDLE hiddenHash = GetPropW(panel, L"Weasel.RenderHash");
  auto originalComments = ui.ctx().cinfo.comments;
  for (auto& comment : ui.ctx().cinfo.comments) comment.str = L"this annotation must remain invisible";
  ui.Refresh(); PumpPreview();
  bool hiddenStable = hiddenHash && hiddenHash == GetPropW(panel, L"Weasel.RenderHash");
  passed = passed && hiddenStable;
  report << "\nhidden annotation pixels unchanged=" << hiddenStable;
  ui.ctx().cinfo.comments = originalComments;
  RemovePropW(panel, L"Weasel.RenderProbe"); RemovePropW(panel, L"Weasel.RenderGeneration");
  RemovePropW(panel, L"Weasel.RenderHash");
  return passed;
}
static bool PreferenceTests() {
  wchar_t previous[32768] = {}; GetEnvironmentVariableW(L"LOCALAPPDATA", previous, 32768);
  auto temporary = std::filesystem::absolute(std::filesystem::temp_directory_path());
  while (temporary.filename().empty() && temporary != temporary.root_path()) temporary = temporary.parent_path();
  auto sandbox = temporary / (L"RimePreviewPreferences-" + std::to_wstring(GetCurrentProcessId()));
  std::ofstream report("native-preferences-test.log");
  if (sandbox.parent_path() != temporary || std::filesystem::exists(sandbox)) { report << "Sandbox path invalid or already exists"; return false; }
  std::filesystem::create_directories(sandbox / L"ShorekeeperWeasel");
  SetEnvironmentVariableW(L"LOCALAPPDATA", sandbox.c_str());
  bool passed = false;
  try {
    auto root = weasel::SkinDataRoot();
    auto write = [](const std::filesystem::path& path, const char* value) { std::ofstream stream(path); stream << value; };
    auto style = ui.style(); std::string error;
    write(root / L"display.json", R"({"schemaVersion":1,"annotationPosition":"above"})");
    if (!weasel::LoadActiveSkin(style, error) || style.annotation_position != 1) throw std::runtime_error("No-skin preference failed: " + error);
    auto skin = root / L"skins" / L"neutral" / L"1.0.0"; std::filesystem::create_directories(skin);
    write(skin / L"manifest.json", R"({"schemaVersion":1,"minEngineVersion":"1.0","variants":[{"id":"plain","style":{"annotationPosition":"beside"},"layers":[]}]})");
    write(root / L"state.json", R"({"schemaVersion":1,"skinId":"neutral","skinVersion":"1.0.0","variantId":"plain","styleOverride":{"annotationPosition":"hidden","fontPoint":21}})");
    if (!weasel::LoadActiveSkin(style, error) || style.annotation_position != 1 || style.font_point != 21 || !style.skin_images.empty()) throw std::runtime_error("Legacy/pure skin preference failed: " + error);
    write(root / L"display.json", R"({"schemaVersion":1,"annotationPosition":"skin"})");
    if (!weasel::LoadActiveSkin(style, error) || style.annotation_position != 2) throw std::runtime_error("Follow-skin failed");
    write(root / L"display.json", R"({"schemaVersion":1,"annotationPosition":"invalid"})");
    auto before = style;
    if (weasel::LoadActiveSkin(style, error) || style != before) throw std::runtime_error("Failure modified style");
    write(root / L"display.json", R"({"schemaVersion":2,"annotationEnabled":true,"annotationPosition":"above","emptyAnnotationMode":"reserve","fontMode":"user","fontPoint":12,"commentFontPoint":10,"density":"compact"})");
    if (!weasel::LoadActiveSkin(style, error) || style.font_point != 12 || style.label_font_point != 12 || style.comment_font_point != 10 ||
        style.candidate_spacing != 8 || style.margin_x != 4 || style.annotation_gap != 2 || style.annotation_collapse_empty) throw std::runtime_error("V2 compact preference failed: " + error);
    auto padding = style.skin_padding;
    write(root / L"display.json", R"({"schemaVersion":2,"annotationEnabled":false,"fontMode":"skin","density":"standard"})");
    if (!weasel::LoadActiveSkin(style, error) || style.annotation_position != 2 || style.candidate_spacing != 12 || style.margin_x != 6 || style.skin_padding != padding) throw std::runtime_error("V2 density/hidden/padding failed");
    before = style;
    write(root / L"display.json", R"({"schemaVersion":2,"fontPoint":0})");
    if (weasel::LoadActiveSkin(style, error) || style != before) throw std::runtime_error("Invalid V2 preference modified style");
    passed = true;
    report << "PASS no-skin, pure-color, legacy override, follow-skin, V2 fonts/density/visibility and failure rollback";
  } catch (const std::exception& error) { report << error.what(); passed = false; }
  SetEnvironmentVariableW(L"LOCALAPPDATA", *previous ? previous : nullptr);
  if (sandbox.parent_path() == temporary) std::filesystem::remove_all(sandbox);
  return passed;
}
static bool LayoutTests(HWND window) {
  std::ofstream report("native-layout-test.log");
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
    layout.DoLayout(CDCHandle(dc.m_hDC), resources); auto size = layout.GetContentSize();
    for (int i = 0; i < 3; ++i) {
      auto candidate = layout.GetCandidateRect(i), text = layout.GetCandidateTextRect(i);
      report << "dpi=" << dpi << " position=" << position << " item=" << i << " size=" << size.cx << ',' << size.cy
             << " candidate=" << candidate.left << ',' << candidate.top << ',' << candidate.right << ',' << candidate.bottom
             << " text=" << text.left << ',' << text.top << ',' << text.right << ',' << text.bottom << '\n';
      if (candidate.left > text.left || candidate.top > text.top || candidate.right < text.right || candidate.bottom < text.bottom ||
          candidate.left < 0 || candidate.top < 0 || candidate.right > size.cx || candidate.bottom > size.cy) return false;
      auto comment = layout.GetCandidateCommentRect(i);
      if (position == 1) for (int j = 0; j < i; ++j)
        if (layout.GetCandidateRect(j).top == candidate.top && layout.GetCandidateTextRect(j).bottom != text.bottom) return false;
      if (position == 2 && comment.Width()) return false;
      if (position == 1 && i == 0 && comment.bottom >= text.top) return false;
      if (comment.Width() && (candidate.left > comment.left || candidate.right < comment.right || candidate.top > comment.top || candidate.bottom < comment.bottom)) return false;
    }
  }
  for (auto& comment : sample.cinfo.comments) comment.clear();
  auto style = ui.style(); style.annotation_position = 1; style.annotation_collapse_empty = true;
  auto resources = std::make_shared<weasel::DirectWriteResources>(style, 144);
  weasel::HorizontalLayout collapsed(style, sample, state, resources);
  collapsed.DoLayout(CDCHandle(dc.m_hDC), resources);
  style.annotation_collapse_empty = false;
  weasel::HorizontalLayout reserved(style, sample, state, resources);
  reserved.DoLayout(CDCHandle(dc.m_hDC), resources);
  if (reserved.GetContentSize().cy <= collapsed.GetContentSize().cy) return false;
  if (!collapsed.annotation_separators.empty() || reserved.annotation_separators.empty()) return false;
  report << "PASS mixed annotations, row baselines, candidate hit bounds and empty-row collapse/reserve";
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
    label.str = std::to_wstring(i + 1);
    comment.str = long_text && i == 0 ? L"wǒ xiǎng zài zhè lǐ cháng shì yí xià shū rù" : tones[i];
    context.cinfo.candies.push_back(text); context.cinfo.labels.push_back(label); context.cinfo.comments.push_back(comment);
  }
  RECT rect; GetWindowRect(window, &rect);
  RECT cursor{rect.left + 70, rect.top + 260, rect.left + 72, rect.top + 280};
  ui.Update(context, status); ui.UpdateInputPosition(cursor); ui.Show();
}
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
  switch (message) {
    case WM_CREATE: SetTimer(hwnd, 1, smoke_frames ? 50 : 1500, nullptr); break;
    case WM_TIMER:
      long_text = !long_text;
      if (smoke_frames) ui.style().annotation_position = smoke_frames % 3;
      UpdatePreview(hwnd);
      if (smoke_frames && --smoke_frames == 0) DestroyWindow(hwnd);
      break;
    case WM_MOVE: if (ui.pdwr()) UpdatePreview(hwnd); break;
    case WM_KEYDOWN:
      if (w == '1' || w == '2' || w == '3') { ui.style().annotation_position = w == '1' ? 1 : w == '2' ? 0 : 2; ui.Refresh(); }
      if (w == VK_ESCAPE) DestroyWindow(hwnd);
      break;
    case WM_PAINT: {
      PAINTSTRUCT paint; HDC dc = BeginPaint(hwnd, &paint);
      SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(40, 60, 80));
      HFONT font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH, L"Microsoft YaHei UI");
      HGDIOBJ oldFont = SelectObject(dc, font);
      RECT text{30, 30, 780, 150};
      DrawTextW(dc, L"原生皮肤预览 · 不连接输入法、不读取词库\n候选自动切换长短文本；拖动窗口测试屏幕边缘与跨屏\n按 1 上方注音 / 2 旁边注音 / 3 隐藏；Esc 关闭", -1, &text, DT_LEFT);
      SelectObject(dc, oldFont); DeleteObject(font);
      EndPaint(hwnd, &paint); return 0;
    }
    case WM_DESTROY: ui.Destroy(true); PostQuitMessage(0); return 0;
  }
  return DefWindowProc(hwnd, message, w, l);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
  auto previewLogs = std::filesystem::temp_directory_path() / (L"RimePreviewRender-" + std::to_wstring(GetCurrentProcessId()));
  SetEnvironmentVariableW(L"RIME_RENDER_LOG_ROOT", previewLogs.c_str());
  SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); _Module.Init(nullptr, instance);
  int count = 0; LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
  bool layout_test = count == 2 && !wcscmp(args[1], L"--layout-test");
  bool preference_test = count == 2 && !wcscmp(args[1], L"--preferences-test");
  bool redraw_test = count == 2 && !wcscmp(args[1], L"--redraw-test");
  for (int i = 1; i + 1 < count; i += 2) {
    if (!wcscmp(args[i], L"--manifest")) manifest = args[i + 1];
    if (!wcscmp(args[i], L"--variant")) variant = wtou8(args[i + 1]);
    if (!wcscmp(args[i], L"--style-overrides")) overrides = args[i + 1];
    if (!wcscmp(args[i], L"--display-preferences")) display_preferences = args[i + 1];
    if (!wcscmp(args[i], L"--smoke")) smoke_frames = 20;
    if (!wcscmp(args[i], L"--redraw-test")) redraw_test = true;
  }
  LocalFree(args);
  WNDCLASSW cls{}; cls.hInstance = instance; cls.lpfnWndProc = WindowProc;
  cls.lpszClassName = L"ShorekeeperNativePreview"; cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
  cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClassW(&cls);
  HWND window = CreateWindowW(cls.lpszClassName, L"Rime · 原生皮肤预览", WS_OVERLAPPEDWINDOW,
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
  if (preference_test) {
    bool passed = PreferenceTests(); DestroyWindow(window); _Module.Term(); CoUninitialize(); return passed ? 0 : 5;
  }
  std::string error;
  if (!manifest.empty() && !weasel::LoadSkinManifest(manifest, variant, style, error, overrides)) {
    MessageBoxW(window, u8tow(error).c_str(), L"皮肤加载失败", MB_ICONERROR);
    DestroyWindow(window); _Module.Term(); CoUninitialize(); return 1;
  }
  // Use the same defaults as production, without reading the user's active display.json.
  if (display_preferences.empty()) display_preferences = previewLogs / L"display.json";
  if (!weasel::LoadDisplayPreferences(display_preferences, style, error)) {
    MessageBoxW(window, u8tow(error).c_str(), L"显示偏好加载失败", MB_ICONERROR);
    DestroyWindow(window); _Module.Term(); CoUninitialize(); return 1;
  }
  status.composing = true; status.schema_id = L"preview"; status.schema_name = L"隔离预览";
  ui.SetUICallBack([window](size_t* selected, size_t* highlighted, bool*, bool*) {
    if (selected) SetWindowTextW(window, (L"已点击候选 " + std::to_wstring(*selected + 1)).c_str());
    if (highlighted) { context.cinfo.highlighted = (int)*highlighted; UpdatePreview(window); }
  });
  style.hover_type = weasel::UIStyle::HILITE;
  ShowWindow(window, show); UpdatePreview(window);
  if (redraw_test) { bool passed = RedrawTests(window); DestroyWindow(window); _Module.Term(); CoUninitialize(); return passed ? 0 : 6; }
  MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  _Module.Term(); CoUninitialize(); return 0;
}
