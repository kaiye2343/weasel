#pragma once
#include <WeaselIPCData.h>
#include <filesystem>

namespace weasel {
std::filesystem::path SkinDataRoot();
bool LoadSkinManifest(const std::filesystem::path& manifest,
                      const std::string& variant, UIStyle& style,
                      std::string& error,
                      const std::filesystem::path& overrides = {});
// A missing or disabled skin preserves the Rime colors and applies display preferences.
bool LoadActiveSkin(UIStyle& style, std::string& error);
bool LoadDisplayPreferences(const std::filesystem::path& path, UIStyle& style,
                            std::string& error);
}
