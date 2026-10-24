#include <atlbase.h>
#include <atlwin.h>
#include <wtl/atlapp.h>
#include <wtl/atlgdi.h>
#include <wtl/atlmisc.h>
#include <WeaselUI.h>
#include <WeaselSkin.h>
#include <WeaselPerformance.h>
#include "../WeaselUI/HorizontalLayout.h"
#include <shellapi.h>
#include <ShellScalingApi.h>
#pragma comment(lib, "Shcore.lib")
#include <fstream>
#include <stdexcept>
#include <chrono>
#include <algorithm>
#include <vector>
#include "../tests/native_layout_facts.h"

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
static unsigned probe_duration_seconds = 30;
static std::string probe_revision = "unknown";
static bool PublicationTests() {
  Gdiplus::GdiplusStartupInput input;
  ULONG_PTR gdip = 0;
  if (Gdiplus::GdiplusStartup(&gdip, &input, nullptr) != Gdiplus::Ok) return false;
  bool passed = false;
  try {
    const auto root = std::filesystem::absolute(std::filesystem::path(L".tools") /
        (L"style-publication-" + std::to_wstring(GetCurrentProcessId())));
    weasel::SkinAssetPublisher publisher(root / L"cache", root / L"private-state");
    weasel::UIStyle style;
    std::string error;
    if (!weasel::LoadSkinManifest(L"skins/shorekeeper/manifest.json", "watermelon", style, error))
      throw std::runtime_error(error);
    const auto source = style;
    // Both paths share the actual production publishing seam, including legacy background.
    style.skin_frame_image = source.skin_images.at(0).path;
    style.background_image = source.skin_images.at(1).path;
    const auto original = style;
    if (!weasel::PublishSkinStyle(style, publisher, error)) throw std::runtime_error(error);
    auto expected = original;
    for (size_t i = 0; i < expected.skin_images.size(); ++i)
      expected.skin_images[i].path = style.skin_images[i].path;
    expected.skin_frame_image = style.skin_frame_image;
    expected.background_image.clear();
    if (style != expected) throw std::runtime_error("publication changed layout or colors");
    for (const auto& file : weasel::SkinStyleAssets(style))
      if (std::filesystem::path(file).parent_path() != publisher.Root())
        throw std::runtime_error("source path leaked to client style");
    auto legacy = original;
    legacy.skin_frame_image.clear();
    if (!weasel::PublishSkinStyle(legacy, publisher, error) ||
        std::filesystem::path(legacy.background_image).parent_path() != publisher.Root())
      throw std::runtime_error("legacy background was not published");
    auto invalid = original;
    invalid.skin_images.back().path += L".missing.png";
    const auto before = invalid;
    if (weasel::PublishSkinStyle(invalid, publisher, error) || invalid != before)
      throw std::runtime_error("failed publication changed partial style");
    weasel::UIStyle plain;
    const auto plainBefore = plain;
    if (!weasel::PublishSkinStyle(plain, publisher, error) || plain != plainBefore)
      throw std::runtime_error("pure color publication changed style");
    std::ofstream("native-publication-test.log") << "PASS decoration/frame/background publication, unchanged geometry, pure colors and atomic failure\n";
    passed = true;
  } catch (const std::exception& error) {
    std::ofstream("native-publication-test.log") << error.what() << '\n';
  }
  Gdiplus::GdiplusShutdown(gdip);
  return passed;
}
static void PumpPreview() { MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } }
static bool PerformanceProbeTests(HWND owner) {
  // Isolated synthetic UI only: no Rime sessions, input events or user settings.
  namespace perf = weasel::performance;
  using Clock = std::chrono::steady_clock;
  KillTimer(owner, 1);
  const auto originalContext = context;
  HWND panel = ui.GetCandidateWindow();
  std::ofstream output("native-performance-probe-test.json", std::ios::binary);
  output.imbue(std::locale::classic());
  bool passed = false;
  try {
    if (!panel || context.cinfo.candies.size() < 2 || !output)
      throw std::runtime_error("Probe fixture is unavailable");
    UINT dpiX = 0, dpiY = 0;
    if (FAILED(GetDpiForMonitor(MonitorFromWindow(panel, MONITOR_DEFAULTTONEAREST),
                               MDT_EFFECTIVE_DPI, &dpiX, &dpiY)))
      throw std::runtime_error("Cannot determine probe monitor DPI");
    perf::Options options;
    options.sourceRevision = probe_revision;
    options.pid = GetCurrentProcessId();
    options.backend = "gdi";
    options.dpi = dpiX;
    options.scenarioId = "native.preview.hot";
    options.durationSeconds = probe_duration_seconds;
    options.available[static_cast<size_t>(perf::Stage::Layout)] = true;
    options.available[static_cast<size_t>(perf::Stage::ResourcePrepare)] = true;
    options.available[static_cast<size_t>(perf::Stage::PaintSubmit)] = true;
    ULONG_PTR referenceHash = 0;
    SIZE referenceSize{};
    output << "{\"schemaVersion\":1,\"runs\":[";
    for (int group = 0; group < 3; ++group) for (int phase = 0; phase < 4; ++phase) {
      const bool enabled = phase == 1 || phase == 2;
      std::string error;
      if (enabled && !perf::GetProcessProbe().Start(options, error))
        throw std::runtime_error(error);
      const auto start = Clock::now();
      for (size_t frame = 0; frame < 200; ++frame) {
        context.cinfo.highlighted = static_cast<int>((frame + 1) % context.cinfo.candies.size());
        ui.Update(context, status);
        PumpPreview();
      }
      const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
      perf::Report report;
      if (enabled) {
        if (!perf::GetProcessProbe().Enabled() || elapsed >= options.durationSeconds * 1000.0)
          throw std::runtime_error("ABBA sampling interval expired before the trial completed");
        if (!perf::GetProcessProbe().Finish(report, error)) throw std::runtime_error(error);
        for (auto stage : {perf::Stage::Layout, perf::Stage::ResourcePrepare, perf::Stage::PaintSubmit})
          if (!report.stages[static_cast<size_t>(stage)].samples)
            throw std::runtime_error("A real preview timing hook did not run");
        if (!report.counters[static_cast<size_t>(perf::Counter::FrameSubmit)])
          throw std::runtime_error("No real frame was submitted");
      } else if (perf::GetProcessProbe().Enabled()) {
        throw std::runtime_error("Probe was unexpectedly enabled");
      }
      // Pixel hashing is deliberately outside both the timing and sampling interval.
      if (!SetPropW(panel, L"Weasel.RenderProbe", reinterpret_cast<HANDLE>(1)))
        throw std::runtime_error("Cannot enable synthetic pixel check");
      ui.Hide(); ui.Show(); PumpPreview();
      if (!GetPropW(panel, L"Weasel.RenderGeneration"))
        throw std::runtime_error("No pixel check frame was drawn");
      const HANDLE hashValue = GetPropW(panel, L"Weasel.RenderHash");
      if (!hashValue) throw std::runtime_error("Cannot prove pixel equivalence without a frame hash");
      const auto hash = reinterpret_cast<ULONG_PTR>(hashValue);
      RECT bounds{};
      if (!GetWindowRect(panel, &bounds)) throw std::runtime_error("Cannot read preview bounds");
      const SIZE size{bounds.right - bounds.left, bounds.bottom - bounds.top};
      if (!group && !phase) { referenceHash = hash; referenceSize = size; }
      else if (hash != referenceHash || size.cx != referenceSize.cx || size.cy != referenceSize.cy)
        throw std::runtime_error("Final synthetic redraws differ across trials");
      RemovePropW(panel, L"Weasel.RenderProbe");
      RemovePropW(panel, L"Weasel.RenderGeneration");
      RemovePropW(panel, L"Weasel.RenderHash");
      if (group || phase) output << ',';
      output << "{\"group\":" << group + 1 << ",\"phase\":" << phase + 1
             << ",\"mode\":\"" << (enabled ? "enabled" : "disabled")
             << "\",\"frames\":200,\"wallMs\":" << elapsed << ",\"probe\":";
      if (enabled) perf::WriteReport(output, report); else output << "null";
      output << '}';
    }
    output << "],\"finalRedrawEquivalent\":true}\n";
    output.close();
    passed = output.good();
    std::ofstream("native-performance-probe-test.log") << (passed ? "PASS" : "FAIL")
        << " native hot-frame probe: three ABBA groups, equivalent post-sampling redraws, no Rime data\n";
  } catch (const std::exception& error) {
    std::ofstream("native-performance-probe-test.log") << "FAIL " << error.what() << '\n';
    perf::Report discarded;
    std::string ignored;
    perf::GetProcessProbe().Finish(discarded, ignored);
  }
  RemovePropW(panel, L"Weasel.RenderProbe");
  RemovePropW(panel, L"Weasel.RenderGeneration");
  RemovePropW(panel, L"Weasel.RenderHash");
  context = originalContext; ui.Update(context, status);
  return passed;
}
static bool PerformanceTests(HWND owner) {
  // Synthetic data only: no Rime sessions, user dictionaries or input events.
  KillTimer(owner, 1);
  ShowWindow(owner, SW_SHOWNOACTIVATE); ui.Show(); PumpPreview();
  auto original = ui.style();
  auto originalContext = context;
  std::ofstream report("native-performance-test.csv");
  report << "mode,workload,characters,annotation,shadow,layout_p50_ms,layout_p95_ms,frame_p50_ms,frame_p95_ms\n";
  using Clock = std::chrono::steady_clock;
  auto milliseconds = [](Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
  };
  auto percentile = [](std::vector<double> values, size_t p) {
    std::sort(values.begin(), values.end()); return values[(values.size() - 1) * p / 100];
  };
  CClientDC dc(owner);
  HWND panel = ui.GetCandidateWindow();
  for (int imageMode : {0, 1, 2}) for (bool resizing : {false, true}) for (bool shadow : {false, true})
    for (int annotation : {1, 2}) for (int length : {4, 16, 32, 64}) {
      ui.style() = original;
      auto& style = ui.style();
      if (!imageMode) { style.skin_images.clear(); style.skin_frame_image.clear(); }
      for (const auto* property : {L"Weasel.DisableImageScaleCache", L"Weasel.DisableStaticSkinCache"}) {
        if (imageMode == 1) SetPropW(panel, property, reinterpret_cast<HANDLE>(1));
        else RemovePropW(panel, property);
      }
      if (!shadow) { style.shadow_color = style.candidate_shadow_color = style.hilited_candidate_shadow_color = 0; }
      style.annotation_position = annotation;
      context.cinfo.candies.clear(); context.cinfo.comments.clear(); context.cinfo.labels.clear();
      for (int i = 0; i < 6; ++i) {
        weasel::Text text, comment, label;
        text.str.assign(length, L'岸');
        for (int j = 0; j < length; ++j) comment.str += L"àn ";
        label.str = std::to_wstring(i + 1);
        context.cinfo.candies.push_back(text); context.cinfo.comments.push_back(comment); context.cinfo.labels.push_back(label);
      }
      context.preedit.str.assign(length * 2, L'a');
      auto resources = std::make_shared<weasel::DirectWriteResources>(style, 144);
      std::vector<double> layouts, frames;
      for (int i = 0; i < 15; ++i) {
        if (resizing) context.cinfo.candies[0].str.assign(length + i % 2, L'岸');
        auto start = Clock::now();
        weasel::HorizontalLayout layout(style, context, status, resources);
        layout.DoLayout(CDCHandle(dc.m_hDC), resources);
        double elapsed = milliseconds(start);
        if (i >= 3) layouts.push_back(elapsed);
        context.cinfo.highlighted = i % 6;
        start = Clock::now(); ui.Update(context, status); PumpPreview();
        elapsed = milliseconds(start);
        if (i >= 3) frames.push_back(elapsed);
      }
      report << (imageMode == 2 ? "skin-cached" : imageMode == 1 ? "skin-source" : "no-images") << ',' << (resizing ? "resizing" : "steady") << ',' << length << ',' << annotation << ',' << shadow << ','
             << percentile(layouts, 50) << ',' << percentile(layouts, 95) << ','
             << percentile(frames, 50) << ',' << percentile(frames, 95) << '\n';
    }
  RemovePropW(panel, L"Weasel.DisableImageScaleCache");
  RemovePropW(panel, L"Weasel.DisableStaticSkinCache");
  ui.style() = original; context = originalContext; ui.Update(context, status);
  return report.good(); // Timings are evidence, not a hardware-independent FPS gate.
}
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
  ui.style().annotation_position = 1; ui.style().annotation_separator_enabled = true;
  ui.Refresh(); PumpPreview();
  HANDLE separatorHash = GetPropW(panel, L"Weasel.RenderHash");
  ui.style().annotation_separator_enabled = false; ui.Refresh(); PumpPreview();
  bool separatorVisible = separatorHash && separatorHash != GetPropW(panel, L"Weasel.RenderHash");
  passed = passed && separatorVisible;
  report << "\nannotation separator changes pixels=" << separatorVisible;
  // The cached base must match a fresh draw exactly, while candidate highlights
  // still change. A style change must rebuild the base even with stable text.
  auto originalHighlight = ui.ctx().cinfo.highlighted;
  ui.style().back_color ^= 0x00010101;
  ui.Refresh(); PumpPreview();
  auto cacheGeneration = [panel]() { return reinterpret_cast<ULONG_PTR>(GetPropW(panel, L"Weasel.StaticCacheGeneration")); };
  auto beforeCache = cacheGeneration();
  ui.ctx().cinfo.highlighted = (originalHighlight + 1) % ui.ctx().cinfo.candies.size();
  ui.Refresh(); PumpPreview();
  HANDLE cachedHash = GetPropW(panel, L"Weasel.RenderHash");
  bool reusedBase = beforeCache && beforeCache == cacheGeneration();
  SetPropW(panel, L"Weasel.DisableStaticSkinCache", reinterpret_cast<HANDLE>(1));
  ui.Hide(); ui.Show(); PumpPreview();
  bool identicalBase = cachedHash && cachedHash == GetPropW(panel, L"Weasel.RenderHash");
  RemovePropW(panel, L"Weasel.DisableStaticSkinCache");
  ui.ctx().cinfo.highlighted = originalHighlight; ui.Refresh(); PumpPreview();
  bool dynamicHighlight = cachedHash != GetPropW(panel, L"Weasel.RenderHash");
  beforeCache = cacheGeneration();
  ui.style().back_color ^= 0x00010101; ui.Refresh(); PumpPreview();
  bool invalidatedBase = cacheGeneration() > beforeCache;
  passed = passed && reusedBase && identicalBase && dynamicHighlight && invalidatedBase;
  report << "\nstatic base reused=" << reusedBase << " pixels identical=" << identicalBase
         << " dynamic highlight=" << dynamicHighlight << " style invalidates=" << invalidatedBase;
  RemovePropW(panel, L"Weasel.RenderProbe"); RemovePropW(panel, L"Weasel.RenderGeneration");
  RemovePropW(panel, L"Weasel.RenderHash");
  RemovePropW(panel, L"Weasel.StaticCacheGeneration");
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
    write(skin / L"manifest.json", R"({"schemaVersion":2,"minEngineVersion":"1.1","annotationSupport":"none","variants":[{"id":"plain","style":{"annotationPosition":"above"},"layers":[]}]})");
    write(root / L"display.json", R"({"schemaVersion":2,"annotationEnabled":true,"annotationPosition":"above"})");
    if (!weasel::LoadActiveSkin(style, error) || style.skin_supports_annotation || style.annotation_position != 2) throw std::runtime_error("Plain-only capability overridden");
    write(skin / L"manifest.json", R"({"schemaVersion":2,"minEngineVersion":"1.1","annotationSupport":"both","variants":[{"id":"plain","style":{},"layers":[]}]})");
    if (!weasel::LoadActiveSkin(style, error) || !style.skin_supports_annotation || style.annotation_position != 1) throw std::runtime_error("Both-layout capability failed");
    before = style;
    write(skin / L"manifest.json", R"({"schemaVersion":2,"minEngineVersion":"1.1","variants":[{"id":"plain","style":{},"layers":[]}]})");
    if (weasel::LoadActiveSkin(style, error) || style != before) throw std::runtime_error("Missing capability committed incomplete style");
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
  sample.preedit.str = L"shou'an'ren";
  {
    auto style = ui.style(); style.annotation_position = 1; style.inline_preedit = true;
    style.client_caps &= ~weasel::INLINE_PREEDIT_CAPABLE;
    auto resources = std::make_shared<weasel::DirectWriteResources>(style, 144);
    weasel::HorizontalLayout fallback(style, sample, state, resources);
    fallback.DoLayout(CDCHandle(dc.m_hDC), resources);
    style.client_caps |= weasel::INLINE_PREEDIT_CAPABLE;
    weasel::HorizontalLayout inlineLayout(style, sample, state, resources);
    inlineLayout.DoLayout(CDCHandle(dc.m_hDC), resources);
    if (fallback.GetPreeditRect().IsRectEmpty() || !inlineLayout.GetPreeditRect().IsRectEmpty() ||
        inlineLayout.GetCandidateCommentRect(0).IsRectEmpty() || inlineLayout.GetContentSize().cy >= fallback.GetContentSize().cy) return false;
    report << "PASS inline composition removes duplicate code row and preserves annotations; legacy fallback retained\n";
  }
  {
    auto style = ui.style(); style.annotation_position = 1; style.skin_supports_annotation = false; style.annotation_collapse_empty = false;
    auto resources = std::make_shared<weasel::DirectWriteResources>(style, 144);
    weasel::HorizontalLayout plainOnly(style, sample, state, resources); plainOnly.DoLayout(CDCHandle(dc.m_hDC), resources);
    for (int i = 0; i < 3; ++i) if (!plainOnly.GetCandidateCommentRect(i).IsRectEmpty()) return false;
    if (!plainOnly.annotation_separators.empty()) return false;
    report << "PASS unsupported annotations remain hidden even in reserve mode\n";
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
  POINT origin{70, 260}; ClientToScreen(window, &origin);
  RECT cursor{origin.x, origin.y, origin.x + 150, origin.y + 24};
  ui.Update(context, status); ui.UpdateInputPosition(cursor); ui.Show();
}
static bool PlacementTests(HWND owner) {
  std::ofstream report("native-placement-test.log");
  KillTimer(owner, 1);
  UpdatePreview(owner);
  HWND panel = ui.GetCandidateWindow();
  SetPropW(panel, L"Weasel.RenderProbe", reinterpret_cast<HANDLE>(1));
  auto reads = reinterpret_cast<ULONG_PTR>(GetPropW(panel, L"Weasel.PlacementReads"));
  RECT expected{}; GetWindowRect(panel, &expected);
  bool stable = true;
  for (int i = 0; i < 30; ++i) {
    ui.Refresh(); ui.Show(); PumpPreview();
    RECT actual{}; GetWindowRect(panel, &actual);
    stable = stable && EqualRect(&expected, &actual);
  }
  bool noKeyReads = reinterpret_cast<ULONG_PTR>(GetPropW(panel, L"Weasel.PlacementReads")) == reads;
  POINT origin{70, 260}; ClientToScreen(owner, &origin);
  RECT jitter{origin.x, origin.y + 2, origin.x + 150, origin.y + 26};
  ui.UpdateInputPosition(jitter); ui.Refresh(); PumpPreview();
  RECT jittered{}; GetWindowRect(panel, &jittered);
  stable = stable && EqualRect(&expected, &jittered);
  // The OS/owner can hide the HWND without changing UIImpl::shown. Exercise
  // that boundary directly; an empty candidate list does not guarantee it.
  ShowWindow(panel, SW_HIDE);
  bool internallyHidden = !IsWindowVisible(panel);
  for (int i = 0; i < 10; ++i) { ShowWindow(panel, SW_HIDE); ui.Show(); ui.Refresh(); }
  noKeyReads = noKeyReads && internallyHidden && reinterpret_cast<ULONG_PTR>(GetPropW(panel, L"Weasel.PlacementReads")) == reads;
  report << "placement_reads_before=" << reads << " after_owner_hides=" << reinterpret_cast<ULONG_PTR>(GetPropW(panel, L"Weasel.PlacementReads")) << '\n';
  // Hiding/showing reloads only local appearance preferences, not the caret.
  ui.Hide(); ui.Show(); PumpPreview();
  RECT reshown{}; GetWindowRect(panel, &reshown);
  stable = stable && EqualRect(&expected, &reshown);
  report << "refresh_and_reshow_preserve_caret_anchor=" << stable << '\n';
  report << "visible_and_internally_hidden_updates_do_not_read_preferences=" << noKeyReads << '\n';
  weasel::CandidatePlacement value;
  std::string error;
  auto path = std::filesystem::temp_directory_path() / (L"RimePlacement-" + std::to_wstring(GetCurrentProcessId()) + L".json");
  { std::ofstream file(path); file << R"({"schemaVersion":2,"candidatePlacement":"above","candidateOffsetX":-20,"candidateGap":18,"avoidDecorations":false})"; }
  bool parsed = weasel::LoadCandidatePlacement(path, value, error) && value.preference == 2 && value.offset_x == -20 && value.gap == 18 && !value.avoid_decorations;
  { std::ofstream file(path); file << R"({"candidateGap":-1})"; }
  bool preserved = !weasel::LoadCandidatePlacement(path, value, error) && value.gap == 18 && value.offset_x == -20;
  std::filesystem::remove(path);
  report << "placement_parse_and_failure_preserve_previous=" << (parsed && preserved) << '\n';
  weasel::TraceCandidatePresentation("host_selected", false, false, 3, S_OK);
  weasel::TraceCandidatePresentation("native_selected", true, true, 3, S_OK);
  RECT originalCaret{origin.x, origin.y, origin.x + 150, origin.y + 24};
  ui.UpdateInputPosition(originalCaret);
  ui.Destroy(); ui.Create(owner); ui.Refresh(); ui.Show(); PumpPreview();
  RECT recreated{}; GetWindowRect(ui.GetCandidateWindow(), &recreated);
  bool recreation = EqualRect(&expected, &recreated);
  report << "recreated_window_replays_source_caret_and_preview_path=" << recreation << '\n';
  return stable && noKeyReads && parsed && preserved && recreation && report.good();
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
      RECT input{70, 260, 220, 284};
      DrawTextW(dc, L"shou'an'ren │", -1, &input, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
      MoveToEx(dc, 70, 286, nullptr); LineTo(dc, 220, 286);
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
  if (count > 1 && !wcscmp(args[1], L"--layout-facts")) {
    const bool passed = count == 4 && native_layout_facts::Run(args[2], args[3]);
    LocalFree(args); _Module.Term(); CoUninitialize();
    return passed ? 0 : 10;
  }
  if (count == 2 && !wcscmp(args[1], L"--publication-test")) {
    LocalFree(args);
    const bool passed = PublicationTests();
    _Module.Term(); CoUninitialize();
    return passed ? 0 : 9;
  }
  bool layout_test = count == 2 && !wcscmp(args[1], L"--layout-test");
  bool preference_test = count == 2 && !wcscmp(args[1], L"--preferences-test");
  bool redraw_test = count == 2 && !wcscmp(args[1], L"--redraw-test");
  bool performance_test = count == 2 && !wcscmp(args[1], L"--performance-test");
  bool performance_probe_test = count == 2 && !wcscmp(args[1], L"--performance-probe-test");
  bool invalid_probe_options = false;
  bool probe_options_given = false;
  if (count > 1 && (!wcscmp(args[count - 1], L"--probe-duration") ||
                    !wcscmp(args[count - 1], L"--probe-revision"))) invalid_probe_options = true;
  bool placement_test = count == 2 && !wcscmp(args[1], L"--placement-test");
  for (int i = 1; i + 1 < count; i += 2) {
    if (!wcscmp(args[i], L"--manifest")) manifest = args[i + 1];
    if (!wcscmp(args[i], L"--variant")) variant = wtou8(args[i + 1]);
    if (!wcscmp(args[i], L"--style-overrides")) overrides = args[i + 1];
    if (!wcscmp(args[i], L"--display-preferences")) display_preferences = args[i + 1];
    if (!wcscmp(args[i], L"--smoke")) smoke_frames = 20;
    if (!wcscmp(args[i], L"--redraw-test")) redraw_test = true;
    if (!wcscmp(args[i], L"--performance-test")) performance_test = true;
    if (!wcscmp(args[i], L"--performance-probe-test")) performance_probe_test = true;
    if (!wcscmp(args[i], L"--probe-revision")) { probe_options_given = true; probe_revision = wtou8(args[i + 1]); }
    if (!wcscmp(args[i], L"--probe-duration")) {
      probe_options_given = true;
      const std::wstring seconds = args[i + 1];
      unsigned value = 0;
      if (seconds.empty() || seconds.size() > 3) invalid_probe_options = true;
      else for (wchar_t digit : seconds) {
        if (digit < L'0' || digit > L'9') { invalid_probe_options = true; break; }
        value = value * 10 + static_cast<unsigned>(digit - L'0');
      }
      if (value < 1 || value > 120) invalid_probe_options = true;
      else probe_duration_seconds = value;
    }
    if (!wcscmp(args[i], L"--placement-test")) placement_test = true;
  }
  LocalFree(args);
  if (probe_revision != "unknown" && (probe_revision.size() != 40 ||
      !std::all_of(probe_revision.begin(), probe_revision.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
      }))) invalid_probe_options = true;
  if (invalid_probe_options || (probe_options_given && !performance_probe_test)) {
    _Module.Term(); CoUninitialize(); return 2;
  }
  WNDCLASSW cls{}; cls.hInstance = instance; cls.lpfnWndProc = WindowProc;
  cls.lpszClassName = L"ShorekeeperNativePreview"; cls.hCursor = LoadCursor(nullptr, IDC_ARROW);
  cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); RegisterClassW(&cls);
  HWND window = CreateWindowW(cls.lpszClassName, L"Rime · 原生皮肤预览", WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1000, 650, nullptr, nullptr, instance, nullptr);
  auto& style = ui.style();
  style.font_face = style.label_font_face = style.comment_font_face = L"Microsoft YaHei UI";
  style.font_point = style.label_font_point = 16; style.comment_font_point = 11;
  style.layout_type = weasel::UIStyle::LAYOUT_HORIZONTAL; style.inline_preedit = true;
  style.client_caps |= weasel::INLINE_PREEDIT_CAPABLE; // Simulate normal TSF inline composition.
  style.back_color = 0xfffffbf5; style.candidate_text_color = style.text_color = 0xff432f1c;
  style.label_text_color = style.comment_text_color = 0xff997f61;
  style.hilited_candidate_text_color = style.hilited_label_text_color = 0xffc87b20;
  style.hilited_comment_text_color = 0xff997f61; style.hilited_candidate_back_color = 0xffffeedd;
  style.border_color = 0xffe9a94d; style.shadow_color = 0x30000000;
  style.hilite_spacing = 5; style.hilite_padding_x = 3; style.hilite_padding_y = 3; style.max_width = 800;
  if (display_preferences.empty()) display_preferences = previewLogs / L"display.json";
  ui.SetPlacementPreferencesPath(display_preferences);
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
  ui.SetPlacementPreferencesPath(display_preferences);
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
  if (placement_test) { bool passed = PlacementTests(window); DestroyWindow(window); _Module.Term(); CoUninitialize(); return passed ? 0 : 8; }
  if (performance_test) { bool passed = PerformanceTests(window); DestroyWindow(window); _Module.Term(); CoUninitialize(); return passed ? 0 : 7; }
  if (performance_probe_test) { bool passed = PerformanceProbeTests(window); DestroyWindow(window); _Module.Term(); CoUninitialize(); return passed ? 0 : 10; }
  if (redraw_test) { bool passed = RedrawTests(window); DestroyWindow(window); _Module.Term(); CoUninitialize(); return passed ? 0 : 6; }
  MSG msg; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  _Module.Term(); CoUninitialize(); return 0;
}
