// Isolated preview CLI only. No HWND, user settings, dictionary, or input session.
#pragma once
#include <boost/json.hpp>
#include "../WeaselUI/SkinGeometry.h"

namespace native_layout_facts {
namespace json = boost::json;
inline json::array Rect(const CRect& r) { return {r.left, r.top, r.right, r.bottom}; }
class FixedLayout : public weasel::HorizontalLayout {
 public:
  FixedLayout(const weasel::UIStyle& style, const weasel::Context& context,
              const weasel::Status& status, weasel::PDWR resources,
              const json::object& metrics)
      : HorizontalLayout(style, context, status, resources), metrics_(metrics) {}
  void GetTextSizeDW(const std::wstring& text, size_t count,
                    ComPtr<IDWriteTextFormat1>& format, weasel::PDWR resources,
                    LPSIZE size) const override {
    const bool comment = format.Get() == resources->pCommentTextFormat.Get();
    const float scale = resources->dpiScaleLayout;
    size->cx = static_cast<LONG>(static_cast<double>(count) *
        static_cast<double>(metrics_.at(comment ? "commentAdvance" : "textAdvance").as_int64()) * scale);
    size->cy = text.empty() ? 0 : static_cast<LONG>(
        static_cast<double>(metrics_.at(comment ? "commentHeight" : "textHeight").as_int64()) * scale);
  }
 private:
  const json::object& metrics_;
};
inline bool Run(const std::filesystem::path& inputPath,
                const std::filesystem::path& outputPath) {
  // A compatible memory DC is sufficient for production round-region checks.
  Gdiplus::GdiplusStartupInput startup;
  ULONG_PTR token = 0;
  if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok) return false;
  HDC memory = CreateCompatibleDC(nullptr);
  bool passed = false;
  try {
    if (!memory || std::filesystem::file_size(inputPath) > 1024 * 1024)
      throw std::runtime_error("Invalid isolated layout input");
    std::ifstream stream(inputPath, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(stream)), {});
    auto input = json::parse(bytes).as_object();
    const bool sizeFacts = input.at("scope").as_string() == "candidate-size-occlusion";
    if (input.at("schemaVersion").as_int64() != 1 ||
        (!sizeFacts && input.at("scope").as_string() != "decoration-anchors") ||
        input.at("cases").as_array().size() > 64)
      throw std::runtime_error("Unsupported isolated layout fixture");
    json::array results;
    for (bool fixed : {true, false}) for (const auto& dpiValue : input.at("dpi").as_array()) {
      const auto dpiNumber = dpiValue.as_int64();
      if (dpiNumber != 96 && dpiNumber != 144 && dpiNumber != 192)
        throw std::runtime_error("Unsupported fixture DPI");
      const UINT dpi = static_cast<UINT>(dpiNumber);
      const auto q = [dpi](int value) { return static_cast<int>(value * (static_cast<float>(dpi) / 96.0f)); };
      for (const auto& value : input.at("cases").as_array()) {
        const auto& item = value.as_object();
        weasel::UIStyle style;
        style.layout_type = weasel::UIStyle::LAYOUT_HORIZONTAL;
        style.inline_preedit = true;
        style.client_caps = weasel::INLINE_PREEDIT_CAPABLE;
        style.font_face = style.label_font_face = style.comment_font_face = L"Microsoft YaHei UI";
        style.font_point = style.label_font_point = 12;
        style.comment_font_point = 10;
        style.label_text_format = L"%s.";
        style.margin_x = style.margin_y = 4;
        style.candidate_spacing = 8;
        style.hilite_spacing = 2;
        style.hilite_padding_x = 4; style.hilite_padding_y = 3;
        style.skin_padding = {4, 4, 4, 4};
        // Synthetic fixture parameters only; never read a user's active skin.
        if (sizeFacts) {
          const auto& spec = item.at("style").as_object();
          auto number = [&spec](const char* key, int low, int high) {
            const auto n = spec.at(key).as_int64();
            if (n < low || n > high) throw std::runtime_error("Invalid size fixture style");
            return static_cast<int>(n);
          };
          style.font_point = style.label_font_point = number("fontPoint", 8, 48);
          style.comment_font_point = number("commentFontPoint", 8, 32);
          style.margin_x = style.margin_y = 0;
          style.skin_padding = {number("paddingLeft", 0, 256), number("paddingTop", 0, 256),
              number("paddingRight", 0, 256), number("paddingBottom", 0, 256)};
          style.candidate_spacing = number("candidateSpacing", 0, 256);
        }
        style.comment_text_color = style.hilited_comment_text_color = static_cast<int>(0xff607d8bu);
        style.annotation_position = item.at("annotationPosition").as_string() == "above" ? 1 :
            item.at("annotationPosition").as_string() == "hidden" ? 2 : 0;
        style.annotation_collapse_empty = item.at("emptyAnnotationMode").as_string() == "collapse";
        weasel::Context context;
        weasel::Status status; status.composing = true;
        const auto& candidates = item.at("candidates").as_array();
        if (candidates.empty() || candidates.size() > 10) throw std::runtime_error("Invalid candidate count");
        for (const auto& c : candidates) {
          const auto& candidate = c.as_object();
          weasel::Text text, comment, label;
          const auto& textValue = candidate.at("text").as_string();
          const auto& commentValue = candidate.at("comment").as_string();
          text.str = u8tow(std::string(textValue.data(), textValue.size()));
          comment.str = u8tow(std::string(commentValue.data(), commentValue.size()));
          if (text.str.size() > 64 || comment.str.size() > 128) throw std::runtime_error("Invalid synthetic text length");
          label.str = std::to_wstring(context.cinfo.candies.size() + 1);
          context.cinfo.candies.push_back(text); context.cinfo.comments.push_back(comment);
          context.cinfo.labels.push_back(label);
        }
        auto resources = std::make_shared<weasel::DirectWriteResources>(style, dpi);
        if (!resources->pTextFormat || !resources->pLabelTextFormat || !resources->pCommentTextFormat)
          throw std::runtime_error("Native font resources unavailable");
        // Layout has no virtual destructor: retain the derived shared deleter.
        std::shared_ptr<weasel::HorizontalLayout> layout;
        const auto& measurements = sizeFacts ? item.at("measurements").as_object() : input.at("measurements").as_object();
        if (fixed) layout = std::make_shared<FixedLayout>(style, context, status, resources, measurements);
        else layout = std::make_shared<weasel::HorizontalLayout>(style, context, status, resources);
        layout->DoLayout(CDCHandle(memory), resources);
        json::array cells, separators;
        const auto size = layout->GetContentSize();
        for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
          const auto candidate = layout->GetCandidateRect(i);
          const auto text = layout->GetCandidateTextRect(i);
          const auto label = layout->GetCandidateLabelRect(i);
          const auto comment = layout->GetCandidateCommentRect(i);
          CRect draw = candidate;
          draw.InflateRect(q(style.hilite_padding_x), q(style.hilite_padding_y));
          CRect hit = draw; hit.OffsetRect(0, q(7));
          const CPoint origin(q(20), q(10));
          CRect windowHit = hit; windowHit.OffsetRect(origin);
          // Use ATL's real half-open rectangle test; do not apply float tolerance.
          if (!hit.PtInRect(CPoint(hit.left, hit.top)) ||
              hit.PtInRect(CPoint(hit.right, hit.top)) ||
              hit.PtInRect(CPoint(hit.left, hit.bottom))) throw std::runtime_error("Native half-open contract failed");
          cells.push_back(json::object{{"layout", Rect(candidate)}, {"label", Rect(label)},
            {"text", Rect(text)}, {"comment", Rect(comment)}, {"textBottom", text.bottom},
            {"draw", Rect(draw)}, {"hit", Rect(hit)}, {"windowHit", Rect(windowHit)}});
        }
        for (const auto& separator : layout->annotation_separators) separators.push_back(Rect(separator));
        json::object row{{"id", item.at("id")}, {"dpi", dpi},
          {"measurement", fixed ? "fixed" : "directwrite"}, {"size", json::array{size.cx, size.cy}},
          {"origin", json::array{q(20), q(10)}}, {"reorderY", q(7)},
          {"padding", json::array{q(4), q(3)}}, {"cells", cells}, {"separators", separators}};
        if (sizeFacts) {
          const CRect frame = layout->GetContentRect();
          weasel::SkinRect bounds{0, 0, size.cx, size.cy};
          json::array images;
          if (item.at("layers").as_array().size() > 8) throw std::runtime_error("Too many fixture layers");
          for (const auto& layerValue : item.at("layers").as_array()) {
            const auto& layer = layerValue.as_object();
            const auto anchor = layer.at("anchor").as_string();
            auto scaled = [&](const char* key) {
              const auto n = layer.at(key).as_int64();
              if (n < -512 || n > 512) throw std::runtime_error("Invalid fixture layer");
              return q(static_cast<int>(n));
            };
            const auto image = weasel::PlaceSkinImage({frame.left, frame.top, frame.right, frame.bottom},
                anchor == "top_right" || anchor == "bottom_right",
                anchor == "bottom_left" || anchor == "bottom_right",
                scaled("x"), scaled("y"), scaled("width"), scaled("height"));
            images.push_back(json::array{image.left, image.top, image.right, image.bottom});
            bounds = weasel::ExpandSkinCanvas(bounds, image);
          }
          row["frame"] = Rect(frame);
          row["images"] = images;
          row["canvasBounds"] = json::array{bounds.left, bounds.top, bounds.right, bounds.bottom};
          row["canvasOrigin"] = json::array{-bounds.left, -bounds.top};
          row["canvasSize"] = json::array{bounds.right - bounds.left, bounds.bottom - bounds.top};
          row["fontPoint"] = style.font_point;
          row["commentFontPoint"] = style.comment_font_point;
          row["fontEm"] = resources->pTextFormat->GetFontSize();
          row["commentEm"] = resources->pCommentTextFormat->GetFontSize();
        }
        results.push_back(row);
      }
    }
    json::object report{{"schemaVersion", 1}, {"scope", sizeFacts ? "candidate-size-occlusion" : "native-text-facts"},
      {"coordinateSpace", "physical-pixels"}, {"font", "Microsoft YaHei UI"},
      {"input", input}, {"cases", results}};
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    output << json::serialize(report) << '\n';
    passed = output.good();
  } catch (const std::exception&) { passed = false; }
  if (memory) DeleteDC(memory);
  Gdiplus::GdiplusShutdown(token);
  return passed;
}
}  // namespace native_layout_facts
