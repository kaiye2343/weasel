#include "stdafx.h"
#include <WeaselSkin.h>
#include <WeaselUtility.h>
#include <boost/json.hpp>
#include <fstream>
#include <regex>
#include <gdiplus.h>
#include "SkinAssetPath.h"

namespace weasel {
namespace {
using Object = boost::json::object;
Object ReadObject(const std::filesystem::path& path) {
  if (std::filesystem::file_size(path) > 1024 * 1024)
    throw std::runtime_error("JSON exceeds 1MiB");
  std::ifstream stream(path, std::ios::binary);
  std::string data((std::istreambuf_iterator<char>(stream)), {});
  return boost::json::parse(data).as_object();
}
std::string String(const Object& o, const char* key) {
  return std::string(o.at(key).as_string().c_str());
}
int Number(const Object& o, const char* key, int fallback, int low, int high) {
  auto* value = o.if_contains(key);
  auto n = value ? value->as_int64() : fallback;
  if (n < low || n > high) throw std::runtime_error("Style value out of range");
  return (int)n;
}
std::filesystem::path Asset(const std::filesystem::path& root,
                             const std::string& relative) {
  if (relative.empty() || relative.size() > 240 || relative.find_first_of("\\:") != std::string::npos ||
      relative[0] == '/') throw std::runtime_error("Unsafe asset path");
  std::filesystem::path rel = std::filesystem::u8path(relative);
  for (const auto& part : rel)
    if (part == L".." || part == L".") throw std::runtime_error("Unsafe asset path");
  auto result = root / rel;
  if (result.extension() != L".png" || !std::filesystem::is_regular_file(result))
    throw std::runtime_error("PNG asset missing");
  // Reparse points are rejected, including links in ancestor directories.
  for (auto check = result; check != root && !check.empty(); check = check.parent_path())
    if (GetFileAttributesW(check.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)
      throw std::runtime_error("Linked assets are not allowed");
  return ResolveSkinAssetPath(result.wstring());
}
int Color(const Object& o, const char* key, int fallback) {
  auto* value = o.if_contains(key);
  if (!value) return fallback;
  auto text = std::string(value->as_string().c_str());
  if (!std::regex_match(text, std::regex("#[0-9a-fA-F]{8}")))
    throw std::runtime_error("Colors require #AARRGGBB");
  unsigned long c = std::stoul(text.substr(1), nullptr, 16);
  return (int)((c & 0xff00ff00) | ((c & 0xff) << 16) | ((c >> 16) & 0xff));
}
long long ImageBytes(const std::filesystem::path& path) {
  Gdiplus::Bitmap image(path.c_str());
  if (image.GetLastStatus() != Gdiplus::Ok || !image.GetWidth() ||
      !image.GetHeight() || image.GetWidth() > 2048 || image.GetHeight() > 2048)
    throw std::runtime_error("Invalid PNG or dimensions");
  return (long long)image.GetWidth() * image.GetHeight() * 4;
}
void ApplyStyle(const Object& spec, const std::filesystem::path& root, UIStyle& s) {
  if (auto* separator = spec.if_contains("annotationSeparator")) {
    const auto& line = separator->as_object();
    s.annotation_separator_enabled = line.if_contains("enabled") ? line.at("enabled").as_bool() : true;
    s.annotation_separator_color = Color(line, "color", 0x55000000);
    s.annotation_separator_thickness = Number(line, "thickness", 1, 1, 4);
    s.annotation_separator_fade = line.if_contains("fadeEnds") ? line.at("fadeEnds").as_bool() : true;
  }
  if (auto* font = spec.if_contains("fontFace")) s.font_face = u8tow(font->as_string().c_str());
  s.label_font_face = s.comment_font_face = s.font_face;
  s.font_point = s.label_font_point = Number(spec, "fontPoint", s.font_point, 8, 48);
  s.comment_font_point = Number(spec, "commentFontPoint", 11, 8, 32);
  s.skin_padding = {Number(spec, "paddingLeft", 16, 0, 256), Number(spec, "paddingTop", 12, 0, 256),
                    Number(spec, "paddingRight", 16, 0, 256), Number(spec, "paddingBottom", 12, 0, 256)};
  s.margin_x = s.margin_y = 0;
  s.candidate_spacing = Number(spec, "candidateSpacing", 16, 0, 256);
  s.round_corner = s.round_corner_ex = Number(spec, "cornerRadius", 12, 0, 64);
  s.shadow_radius = Number(spec, "shadowRadius", 6, 0, 32);
  s.border = Number(spec, "borderWidth", 2, 0, 12);
  s.skin_border_inset = Number(spec, "borderInset", 0, 0, 32);
  std::string border = spec.if_contains("borderStyle") ? String(spec, "borderStyle") : "solid";
  if (border != "none" && border != "solid" && border != "dash") throw std::runtime_error("Unknown border style");
  s.skin_border_style = border == "dash" ? 2 : border == "solid" ? 1 : 0;
  std::string annotation = spec.if_contains("annotationPosition") ? String(spec, "annotationPosition") : "beside";
  if (annotation != "above" && annotation != "beside" && annotation != "hidden") throw std::runtime_error("Unknown annotation position");
  s.annotation_position = annotation == "above" ? 1 : annotation == "hidden" ? 2 : 0;
  s.back_color = Color(spec, "backgroundColor", s.back_color);
  s.text_color = s.candidate_text_color = s.label_text_color = Color(spec, "textColor", s.candidate_text_color);
  s.comment_text_color = s.hilited_comment_text_color = Color(spec, "commentColor", s.comment_text_color);
  s.hilited_candidate_back_color = Color(spec, "highlightColor", s.hilited_candidate_back_color);
  s.hilited_candidate_text_color = s.hilited_label_text_color = Color(spec, "highlightTextColor", s.hilited_candidate_text_color);
  s.border_color = Color(spec, "borderColor", s.border_color);
  s.shadow_color = Color(spec, "shadowColor", 0x22201810);
  s.skin_frame_image.clear(); s.skin_frame_slice.clear();
  if (auto* frame = spec.if_contains("frame"); frame && !frame->is_null()) {
    const auto& f = frame->as_object();
    s.skin_frame_image = Asset(root, String(f, "path")).wstring();
    ImageBytes(s.skin_frame_image);
    Gdiplus::Bitmap frameBitmap(s.skin_frame_image.c_str());
    const auto& slice = f.at("slice").as_array();
    if (slice.size() != 4) throw std::runtime_error("Four frame slices required");
    for (auto& n : slice) { auto v = n.as_int64(); if (v < 0 || v > 2048) throw std::runtime_error("Invalid frame slice"); s.skin_frame_slice.push_back((int)v); }
    if (s.skin_frame_slice[0] + s.skin_frame_slice[2] >= (int)frameBitmap.GetWidth() ||
        s.skin_frame_slice[1] + s.skin_frame_slice[3] >= (int)frameBitmap.GetHeight())
      throw std::runtime_error("Empty frame center");
  }
}
}
std::filesystem::path SkinDataRoot() {
  wchar_t local[32768] = {};
  auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768);
  if (!count || count >= 32768) throw std::runtime_error("LOCALAPPDATA unavailable");
  return std::filesystem::path(local) / L"ShorekeeperWeasel";
}
bool LoadSkinManifest(const std::filesystem::path& manifest,
                      const std::string& variant, UIStyle& style, std::string& error,
                      const std::filesystem::path& overrides) {
  try {
    auto data = ReadObject(manifest);
    const auto version = data.at("schemaVersion").as_int64();
    const bool v1 = version == 1 && String(data, "minEngineVersion") == "1.0" && !data.if_contains("annotationSupport");
    const auto support = data.if_contains("annotationSupport") ? String(data, "annotationSupport") : "both";
    const bool v2 = version == 2 && String(data, "minEngineVersion") == "1.1" && (support == "none" || support == "both") && data.if_contains("annotationSupport");
    if (!v1 && !v2) throw std::runtime_error("Unsupported skin version or annotation capability");
    for (auto& item : data.at("variants").as_array()) {
      const auto& v = item.as_object();
      if (String(v, "id") != variant) continue;
      UIStyle next = style;
      next.skin_supports_annotation = support != "none";
      auto root = std::filesystem::absolute(manifest).parent_path();
      ApplyStyle(v.at("style").as_object(), root, next);
      if (!overrides.empty()) ApplyStyle(ReadObject(overrides), root, next);
      next.skin_images.clear(); long long bytes = 0;
      if (!next.skin_frame_image.empty()) bytes += ImageBytes(next.skin_frame_image);
      const auto& layers = v.at("layers").as_array();
      if (layers.size() > 8) throw std::runtime_error("Too many image layers");
      for (const auto& layer : layers) {
        const auto& l = layer.as_object(); SkinImage image;
        image.path = Asset(root, String(l, "path")).wstring(); bytes += ImageBytes(image.path);
        image.anchor = u8tow(String(l, "anchor"));
        if (image.anchor != L"top_left" && image.anchor != L"top_right" && image.anchor != L"bottom_left" && image.anchor != L"bottom_right")
          throw std::runtime_error("Unknown anchor");
        image.x = Number(l, "x", 0, -512, 512); image.y = Number(l, "y", 0, -512, 512);
        image.width = Number(l, "width", 0, 1, 512); image.height = Number(l, "height", 0, 1, 512);
        image.z = Number(l, "z", 0, -100, 100); next.skin_images.push_back(image);
      }
      if (bytes > 64LL * 1024 * 1024) throw std::runtime_error("Decoded skin exceeds 64MiB");
      std::stable_sort(next.skin_images.begin(), next.skin_images.end(), [](const auto& a, const auto& b) { return a.z < b.z; });
      if (!next.skin_supports_annotation) next.annotation_position = 2;
      style = std::move(next); error.clear(); return true;
    }
    throw std::runtime_error("Skin variant missing");
  } catch (const std::exception& e) { error = e.what(); return false; }
}
bool LoadActiveSkin(UIStyle& style, std::string& error) {
  try {
    auto root = SkinDataRoot(); auto statePath = root / L"state.json";
    auto applyDisplay = [&root](UIStyle& target) {
      std::string reason;
      if (!LoadDisplayPreferences(root / L"display.json", target, reason)) throw std::runtime_error(reason);
    };
    if (!std::filesystem::exists(statePath)) { UIStyle next = style; next.skin_supports_annotation = true; applyDisplay(next); style = std::move(next); error.clear(); return true; }
    auto state = ReadObject(statePath);
    if (state.at("schemaVersion").as_int64() != 1) throw std::runtime_error("Unsupported selection version");
    if (!state.if_contains("skinId") || state.at("skinId").is_null()) { UIStyle next = style; next.skin_supports_annotation = true; applyDisplay(next); style = std::move(next); error.clear(); return true; }
    auto id = String(state, "skinId"), version = String(state, "skinVersion");
    if (!std::regex_match(id, std::regex("[a-z0-9][a-z0-9._-]{0,63}")) || id.find("..") != std::string::npos ||
        !std::regex_match(version, std::regex("[0-9]+\\.[0-9]+\\.[0-9]+"))) throw std::runtime_error("Invalid skin identity");
    auto dir = root / L"skins" / std::filesystem::u8path(id) / std::filesystem::u8path(version);
    UIStyle next = style;
    if (!LoadSkinManifest(dir / L"manifest.json", String(state, "variantId"), next, error)) return false;
    if (auto* spec = state.if_contains("styleOverride"); spec && !spec->is_null()) ApplyStyle(spec->as_object(), dir, next);
    long long bytes = next.skin_frame_image.empty() ? 0 : ImageBytes(next.skin_frame_image);
    for (const auto& image : next.skin_images) bytes += ImageBytes(image.path);
    if (bytes > 64LL * 1024 * 1024) throw std::runtime_error("Decoded skin exceeds 64MiB");
    applyDisplay(next);
    style = std::move(next); error.clear(); return true;
  } catch (const std::exception& e) { error = e.what(); return false; }
}
bool LoadDisplayPreferences(const std::filesystem::path& path, UIStyle& style,
                            std::string& error) {
  try {
    Object data = std::filesystem::exists(path) ? ReadObject(path) : Object{};
    const int version = Number(data, "schemaVersion", 2, 1, 2);
    UIStyle next = style;
    auto text = [&data](const char* key, const char* fallback) { return data.if_contains(key) ? String(data, key) : std::string(fallback); };
    auto position = text("annotationPosition", version == 1 ? "skin" : "above");
    if (position != "skin" && position != "above" && position != "beside" && position != "hidden") throw std::runtime_error("Unknown annotation position");
    bool enabled = data.if_contains("annotationEnabled") ? data.at("annotationEnabled").as_bool() : true;
    if (!enabled) next.annotation_position = 2;
    else if (position != "skin") next.annotation_position = position == "above" ? 1 : position == "hidden" ? 2 : 0;
    if (version == 2) {
      auto empty = text("emptyAnnotationMode", "collapse");
      if (empty != "collapse" && empty != "reserve") throw std::runtime_error("Unknown empty annotation mode");
      next.annotation_collapse_empty = empty == "collapse";
      auto fontMode = text("fontMode", "user");
      if (fontMode != "user" && fontMode != "skin") throw std::runtime_error("Unknown font mode");
      int font = Number(data, "fontPoint", 12, 8, 48), comment = Number(data, "commentFontPoint", 10, 8, 32);
      if (fontMode == "user") { next.font_point = next.label_font_point = font; next.comment_font_point = comment; }
      auto density = text("density", "compact");
      if (density != "compact" && density != "standard" && density != "skin") throw std::runtime_error("Unknown display density");
      if (density != "skin") {
        int inset = density == "compact" ? 4 : 6;
        if (next.margin_x >= 0) next.margin_x = inset;
        if (next.margin_y >= 0) next.margin_y = inset;
        next.hilite_padding_x = next.hilite_padding_y = inset;
        next.candidate_spacing = density == "compact" ? 8 : 12;
        next.hilite_spacing = density == "compact" ? 2 : 3;
        next.annotation_gap = density == "compact" ? 2 : 3;
      }
    }
    if (!next.skin_supports_annotation) next.annotation_position = 2;
    style = std::move(next); error.clear(); return true;
  } catch (const std::exception& ex) { error = ex.what(); return false; }
}
}
