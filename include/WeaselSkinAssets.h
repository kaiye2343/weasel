#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace weasel {
// Publishes verified copies of skin image resources into a controlled cache and
// maintains the small recent-reference record used by cache collection.
class SkinAssetPublisher {
 public:
  SkinAssetPublisher(std::filesystem::path root = {},
                     std::filesystem::path stateRoot = {});
  bool Publish(const std::vector<std::wstring>& sources,
               std::vector<std::wstring>& published, std::string& error);
  bool RecordAndCollect(const std::vector<std::wstring>& live,
                        std::string& error);
  const std::filesystem::path& Root() const { return root_; }
  size_t HashReads() const { return hash_reads_; }

 private:
  std::filesystem::path root_;
  std::filesystem::path state_root_;
  size_t hash_reads_ = 0;
};
}  // namespace weasel
