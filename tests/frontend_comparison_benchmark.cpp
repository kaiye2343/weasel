// Compile this identical driver against official 0.17.4 and the fork.
// No Rime, services, user data or input events. Local use is layout-only.
#include <atlbase.h>
#include <atlwin.h>
#include <wtl/atlapp.h>
#include <wtl/atlgdi.h>
#include <wtl/atlmisc.h>
#include <WeaselUI.h>
#include "HorizontalLayout.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

CAppModule _Module;
using Clock = std::chrono::steady_clock;
using namespace weasel;

static double Milliseconds(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

static double Percentile(std::vector<double> values, size_t percent) {
  std::sort(values.begin(), values.end());
  return values[(values.size() - 1) * percent / 100];
}

static UIStyle Style(bool comments, bool above, bool shadow) {
  UIStyle s;
  s.font_face = s.label_font_face = s.comment_font_face = L"Microsoft YaHei UI";
  s.font_point = s.label_font_point = 12; s.comment_font_point = 10;
  s.layout_type = UIStyle::LAYOUT_HORIZONTAL;
  s.inline_preedit = true; s.client_caps = INLINE_PREEDIT_CAPABLE;
  s.back_color = 0xfffafafa; s.border_color = 0xffdddddd;
  s.text_color = s.candidate_text_color = s.label_text_color = 0xff222222;
  s.comment_text_color = s.hilited_comment_text_color = comments ? 0xff888888 : 0;
  s.hilited_candidate_text_color = s.hilited_label_text_color = 0xffa05010;
  s.hilited_candidate_back_color = 0xffffeedd;
  s.shadow_color = shadow ? 0x30000000 : 0;
  s.candidate_shadow_color = s.hilited_candidate_shadow_color = 0;
  s.shadow_radius = shadow ? 4 : 0;
  s.shadow_offset_x = 0; s.shadow_offset_y = 2;
  s.margin_x = s.margin_y = 4; s.hilite_padding_x = s.hilite_padding_y = 2;
  s.candidate_spacing = 8; s.hilite_spacing = 2;
  s.round_corner = 6; s.round_corner_ex = 8; s.border = 1;
  s.max_width = 800; s.label_text_format = L"%s.";
#ifdef WEASEL_FORK_BENCHMARK
  s.annotation_position = !comments ? 2 : above ? 1 : 0;
  s.annotation_separator_enabled = above && comments;
  s.annotation_separator_thickness = 1; s.annotation_gap = 2;
#else
  (void)above;
#endif
  return s;
}

static Context Candidates(int length, int frame, bool resizing) {
  Context c;
  c.preedit.str.assign(length * 2, L'a');
  c.preedit.str += wchar_t(L'a' + frame % 20);
  c.cinfo.highlighted = frame % 6;
  for (int i = 0; i < 6; ++i) {
    Text text, comment, label;
    const int n = length + (resizing && i == 0 ? frame % 2 : 0);
    text.str.assign(n, L'岸');
    // Vary both content and highlight, so UI::Update cannot skip the frame.
    text.str.back() = wchar_t(L'你' + frame % 5);
    for (int j = 0; j < n; ++j) comment.str += L"àn ";
    label.str = std::to_wstring(i + 1);
    c.cinfo.candies.push_back(text); c.cinfo.comments.push_back(comment);
    c.cinfo.labels.push_back(label);
  }
  return c;
}

static void Pump() {
  MSG message;
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message); DispatchMessageW(&message);
  }
}

int main(int argc, char** argv) {
  const bool frames = argc == 3 && std::string(argv[2]) == "--ci-ui";
  if (argc < 2 || argc > 3 || (argc == 3 && !frames)) return 2;
  // Never open synthetic windows on the user's desktop.
  if (frames && (!getenv("GITHUB_ACTIONS") || std::string(getenv("GITHUB_ACTIONS")) != "true")) {
    std::cerr << "Window benchmark is restricted to GitHub Actions.\n"; return 2;
  }
  if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 3;
  _Module.Init(nullptr, GetModuleHandleW(nullptr));
  Gdiplus::GdiplusStartupInput input; ULONG_PTR token = 0;
  if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return 3;
  int result = 0;
  try {
    std::ofstream output(argv[1]);
    if (!output) throw std::runtime_error("Cannot create output");
    output << "backend,stage,characters,comments,above,shadow,resizing,dpi,p50_ms,p95_ms,max_ms,width,height\n";
    CDC dc; dc.CreateCompatibleDC(nullptr);
    if (!dc.m_hDC) throw std::runtime_error("No memory DC");
    Status status; status.composing = true; status.schema_id = L"synthetic";
    for (int above = 0; above <=
#ifdef WEASEL_FORK_BENCHMARK
         1
#else
         0
#endif
         ; ++above) for (bool comments : {false, true}) {
      if (above && !comments) continue;
      for (bool shadow : {false, true}) for (bool resizing : {false, true})
        for (int length : {4, 16, 32, 64}) for (UINT dpi : {96U, 144U, 192U}) {
          auto style = Style(comments, above != 0, shadow);
          auto resources = std::make_shared<DirectWriteResources>(style, dpi);
          if (!resources->pTextFormat || !resources->pCommentTextFormat)
            throw std::runtime_error("Text resources unavailable");
          std::vector<double> layouts, paints;
          CSize lastSize;
          UI ui;
          // Frame comparisons use the runner's actual monitor DPI (normally 96).
          // Explicit 144/192 DPI tests below measure layout only.
          const bool draw = frames && dpi == 96;
          if (draw) {
            ui.style() = style;
            if (!ui.Create(nullptr)) throw std::runtime_error("UI creation failed");
            ui.Update(Candidates(length, 0, resizing), status); ui.Show(); Pump();
            if (!ui.IsShown()) throw std::runtime_error("UI not shown");
          }
          for (int frame = 0; frame < 36; ++frame) {
            auto context = Candidates(length, frame, resizing);
            auto start = Clock::now();
            HorizontalLayout layout(style, context, status, resources);
            layout.DoLayout(CDCHandle(dc.m_hDC), resources);
            auto elapsed = Milliseconds(start);
            lastSize = layout.GetContentSize();
            if (lastSize.cx <= 0 || lastSize.cy <= 0) throw std::runtime_error("Empty layout");
            if (frame >= 6) layouts.push_back(elapsed);
            if (draw) {
              start = Clock::now(); ui.Update(context, status); Pump();
              elapsed = Milliseconds(start);
              if (frame >= 6) paints.push_back(elapsed);
            }
          }
          auto write = [&](const char* stage, const std::vector<double>& times) {
            output <<
#ifdef WEASEL_FORK_BENCHMARK
              "fork"
#else
              "official-0.17.4"
#endif
              << ',' << stage << ',' << length << ',' << comments << ',' << above
              << ',' << shadow << ',' << resizing << ',' << dpi << ','
              << Percentile(times, 50) << ',' << Percentile(times, 95) << ','
              << *std::max_element(times.begin(), times.end()) << ','
              << lastSize.cx << ',' << lastSize.cy << '\n';
          };
          write("layout", layouts); if (draw) write("update-paint-pump", paints);
          if (draw) ui.Destroy(true);
        }
    }
    if (!output.good()) throw std::runtime_error("Output write failed");
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
  Gdiplus::GdiplusShutdown(token); _Module.Term(); CoUninitialize();
  return result;
}
