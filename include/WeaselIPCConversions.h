#pragma once
#include <windows.h>
#include <cstddef>
#include <limits>

namespace weasel {

inline DWORD CheckedPipeSize(size_t value) {
  if (value > (std::numeric_limits<DWORD>::max)()) {
    throw DWORD(ERROR_ARITHMETIC_OVERFLOW);
  }
  return static_cast<DWORD>(value);
}

inline bool TryProtocolIndex(size_t index, DWORD& value) noexcept {
  if (index > (std::numeric_limits<DWORD>::max)()) {
    return false;
  }
  value = static_cast<DWORD>(index);
  return true;
}

}  // namespace weasel
