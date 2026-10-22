#pragma once
#include <WeaselIPCData.h>
#include <WeaselSkinAssets.h>
#include <filesystem>

namespace weasel {
// Local presentation preferences: deliberately outside the UIStyle IPC archive.
struct CandidatePlacement {
  int offset_x = 0, gap = 6, preference = 0; // auto / below / above
  bool avoid_decorations = true;
};
bool LoadCandidatePlacement(const std::filesystem::path& path,
                            CandidatePlacement& placement, std::string& error);
void TraceCandidatePresentation(const char* event, bool nativeAllowed,
                                bool windowCreated, size_t images, HRESULT result);
std::filesystem::path SkinDataRoot();
bool LoadSkinManifest(const std::filesystem::path& manifest,
                      const std::string& variant, UIStyle& style,
                      std::string& error,
                      const std::filesystem::path& overrides = {});
// A missing or disabled skin preserves the Rime colors and applies display preferences.
bool LoadActiveSkin(UIStyle& style, std::string& error);
// Prepare every file before changing any path in the style sent to clients.
bool PublishSkinStyle(UIStyle& style, SkinAssetPublisher& publisher,
                      std::string& error);
std::vector<std::wstring> SkinStyleAssets(const UIStyle& style);
bool LoadDisplayPreferences(const std::filesystem::path& path, UIStyle& style,
                            std::string& error);
}
