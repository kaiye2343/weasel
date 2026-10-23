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

inline bool TryProtocolSessionId(LPARAM input, DWORD& value) noexcept {
  const ULONG_PTR raw = static_cast<ULONG_PTR>(input);
  const DWORD low = static_cast<DWORD>(raw);
  const ULONG_PTR mask =
      static_cast<ULONG_PTR>((std::numeric_limits<DWORD>::max)());
  if (raw <= mask) {
    value = low;
    return true;
  }
  if constexpr (sizeof(ULONG_PTR) > sizeof(DWORD)) {
    if ((low & 0x80000000u) != 0 &&
        raw == (static_cast<ULONG_PTR>(low) | ~mask)) {
      value = low;
      return true;
    }
  }
  return false;
}

}  // namespace weasel
