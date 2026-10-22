#pragma once
#include <map>
#include <string>
#include <utility>

namespace weasel {
// Preparing must not mutate a visible/session style. Publish the complete set
// only after every callback has produced a valid replacement.
template <class Key, class Style, class Prepare>
bool PrepareSkinStyleTransaction(const std::map<Key, Style>& current,
                                 std::map<Key, Style>& proposed,
                                 Prepare prepare, std::string& error) {
  error.clear();
  std::map<Key, Style> staged;
  for (const auto& entry : current) {
    Style next = entry.second;
    if (!prepare(entry.first, entry.second, next, error)) return false;
    staged.emplace(entry.first, std::move(next));
  }
  proposed.swap(staged);
  return true;
}
}  // namespace weasel
