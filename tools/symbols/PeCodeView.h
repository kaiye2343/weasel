#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace weasel::symbols {

struct CodeViewIdentity {
  GUID guid{};
  DWORD age = 0;
  WORD machine = 0;
};

template <class T>
inline T ReadPePod(const std::vector<BYTE>& bytes, size_t offset) {
  if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
    throw std::runtime_error("PE range");
  }
  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}

inline void RequirePeRange(const std::vector<BYTE>& bytes, size_t offset, size_t length) {
  if (offset > bytes.size() || length > bytes.size() - offset) {
    throw std::runtime_error("PE range");
  }
}

inline size_t PeRvaToOffset(const std::vector<BYTE>& bytes,
                            const IMAGE_FILE_HEADER& fileHeader,
                            size_t sectionOffset,
                            DWORD sizeOfHeaders,
                            DWORD rva,
                            DWORD size) {
  const uint64_t rva64 = static_cast<uint64_t>(rva);
  const uint64_t size64 = static_cast<uint64_t>(size);
  const uint64_t end64 = rva64 + size64;
  if (end64 <= static_cast<uint64_t>(sizeOfHeaders)) {
    if (rva64 > bytes.size() || size64 > bytes.size() - rva64) {
      throw std::runtime_error("PE range");
    }
    return static_cast<size_t>(rva64);
  }

  const size_t sectionCount = static_cast<size_t>(fileHeader.NumberOfSections);
  for (size_t i = 0; i < sectionCount; ++i) {
    const uint64_t itemOffset64 = static_cast<uint64_t>(sectionOffset) +
                                  static_cast<uint64_t>(i) * sizeof(IMAGE_SECTION_HEADER);
    if (itemOffset64 > bytes.size()) {
      throw std::runtime_error("PE range");
    }
    const IMAGE_SECTION_HEADER section =
        ReadPePod<IMAGE_SECTION_HEADER>(bytes, static_cast<size_t>(itemOffset64));
    if (rva64 >= static_cast<uint64_t>(section.VirtualAddress)) {
      const uint64_t delta = rva64 - static_cast<uint64_t>(section.VirtualAddress);
      if (delta + size64 <= static_cast<uint64_t>(section.SizeOfRawData)) {
        const uint64_t raw64 = static_cast<uint64_t>(section.PointerToRawData) + delta;
        if (raw64 > bytes.size() || size64 > bytes.size() - raw64) {
          throw std::runtime_error("PE range");
        }
        return static_cast<size_t>(raw64);
      }
    }
  }
  throw std::runtime_error("PE range");
}

inline CodeViewIdentity ReadCodeViewIdentity(const std::vector<BYTE>& bytes) {
  constexpr size_t kMaxPeBytes = static_cast<size_t>(512) * 1024 * 1024;
  if (bytes.empty()) {
    throw std::runtime_error("PE empty");
  }
  if (bytes.size() > kMaxPeBytes) {
    throw std::runtime_error("PE too large");
  }

  const IMAGE_DOS_HEADER dos = ReadPePod<IMAGE_DOS_HEADER>(bytes, 0);
  if (dos.e_magic != static_cast<WORD>(IMAGE_DOS_SIGNATURE)) {
    throw std::runtime_error("PE DOS signature");
  }
  if (dos.e_lfanew < 0) {
    throw std::runtime_error("PE e_lfanew");
  }

  const size_t ntOffset = static_cast<size_t>(dos.e_lfanew);
  const DWORD ntSignature = ReadPePod<DWORD>(bytes, ntOffset);
  if (ntSignature != static_cast<DWORD>(IMAGE_NT_SIGNATURE)) {
    throw std::runtime_error("PE NT signature");
  }

  const uint64_t fileHeaderOffset64 = static_cast<uint64_t>(ntOffset) + sizeof(DWORD);
  if (fileHeaderOffset64 > bytes.size()) {
    throw std::runtime_error("PE range");
  }
  const size_t fileHeaderOffset = static_cast<size_t>(fileHeaderOffset64);
  const IMAGE_FILE_HEADER fileHeader = ReadPePod<IMAGE_FILE_HEADER>(bytes, fileHeaderOffset);

  const uint64_t optionalOffset64 =
      static_cast<uint64_t>(fileHeaderOffset) + sizeof(IMAGE_FILE_HEADER);
  if (optionalOffset64 > bytes.size()) {
    throw std::runtime_error("PE range");
  }
  const size_t optionalOffset = static_cast<size_t>(optionalOffset64);
  const WORD magic = ReadPePod<WORD>(bytes, optionalOffset);

  DWORD sizeOfHeaders = 0;
  IMAGE_DATA_DIRECTORY debugDirectory{};
  if (magic == static_cast<WORD>(IMAGE_NT_OPTIONAL_HDR32_MAGIC)) {
    if (fileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER32)) {
      throw std::runtime_error("PE optional header");
    }
    const IMAGE_OPTIONAL_HEADER32 optional =
        ReadPePod<IMAGE_OPTIONAL_HEADER32>(bytes, optionalOffset);
    if (optional.NumberOfRvaAndSizes <= static_cast<DWORD>(IMAGE_DIRECTORY_ENTRY_DEBUG)) {
      throw std::runtime_error("PE debug directory");
    }
    sizeOfHeaders = optional.SizeOfHeaders;
    debugDirectory = optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
  } else if (magic == static_cast<WORD>(IMAGE_NT_OPTIONAL_HDR64_MAGIC)) {
    if (fileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64)) {
      throw std::runtime_error("PE optional header");
    }
    const IMAGE_OPTIONAL_HEADER64 optional =
        ReadPePod<IMAGE_OPTIONAL_HEADER64>(bytes, optionalOffset);
    if (optional.NumberOfRvaAndSizes <= static_cast<DWORD>(IMAGE_DIRECTORY_ENTRY_DEBUG)) {
      throw std::runtime_error("PE debug directory");
    }
    sizeOfHeaders = optional.SizeOfHeaders;
    debugDirectory = optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
  } else {
    throw std::runtime_error("PE optional magic");
  }

  const WORD numberOfSections = fileHeader.NumberOfSections;
  if (numberOfSections < 1 || numberOfSections > 4096) {
    throw std::runtime_error("PE sections");
  }
  const uint64_t sectionOffset64 = static_cast<uint64_t>(optionalOffset) +
                                   static_cast<uint64_t>(fileHeader.SizeOfOptionalHeader);
  if (sectionOffset64 > bytes.size()) {
    throw std::runtime_error("PE range");
  }
  const size_t sectionOffset = static_cast<size_t>(sectionOffset64);
  const size_t sectionBytes =
      static_cast<size_t>(numberOfSections) * sizeof(IMAGE_SECTION_HEADER);
  RequirePeRange(bytes, sectionOffset, sectionBytes);

  const size_t debugOffset = PeRvaToOffset(bytes, fileHeader, sectionOffset, sizeOfHeaders,
                                           debugDirectory.VirtualAddress, debugDirectory.Size);
  if (debugDirectory.Size == 0u ||
      debugDirectory.Size % static_cast<DWORD>(sizeof(IMAGE_DEBUG_DIRECTORY)) != 0u) {
    throw std::runtime_error("PE debug size");
  }
  RequirePeRange(bytes, debugOffset, static_cast<size_t>(debugDirectory.Size));

  CodeViewIdentity identity{};
  bool found = false;
  const size_t count = static_cast<size_t>(debugDirectory.Size) / sizeof(IMAGE_DEBUG_DIRECTORY);
  for (size_t i = 0; i < count; ++i) {
    const uint64_t entryOffset64 = static_cast<uint64_t>(debugOffset) +
                                   static_cast<uint64_t>(i) * sizeof(IMAGE_DEBUG_DIRECTORY);
    if (entryOffset64 > bytes.size()) {
      throw std::runtime_error("PE range");
    }
    const IMAGE_DEBUG_DIRECTORY entry =
        ReadPePod<IMAGE_DEBUG_DIRECTORY>(bytes, static_cast<size_t>(entryOffset64));
    if (entry.Type != static_cast<DWORD>(IMAGE_DEBUG_TYPE_CODEVIEW)) {
      continue;
    }
    if (entry.SizeOfData < 4u) {
      throw std::runtime_error("PE CodeView record");
    }
    const uint64_t rawStart64 = static_cast<uint64_t>(entry.PointerToRawData);
    const uint64_t rawEnd64 = rawStart64 + static_cast<uint64_t>(entry.SizeOfData);
    if (rawStart64 > bytes.size() || rawEnd64 > bytes.size()) {
      throw std::runtime_error("PE range");
    }
    const size_t rawStart = static_cast<size_t>(rawStart64);
    RequirePeRange(bytes, rawStart, static_cast<size_t>(entry.SizeOfData));
    if (std::memcmp(bytes.data() + rawStart, "RSDS", 4) != 0) {
      continue;
    }
    if (entry.SizeOfData < 24u) {
      throw std::runtime_error("PE CodeView record truncated");
    }
    GUID guid{};
    std::memcpy(&guid, bytes.data() + rawStart + 4, sizeof(GUID));
    DWORD age = 0;
    std::memcpy(&age, bytes.data() + rawStart + 20, sizeof(DWORD));
    if (age == 0u) {
      throw std::runtime_error("PE CodeView age");
    }
    bool zeroGuid = true;
    const BYTE* guidBytes = reinterpret_cast<const BYTE*>(&guid);
    for (size_t b = 0; b < sizeof(GUID); ++b) {
      if (guidBytes[b] != 0) {
        zeroGuid = false;
        break;
      }
    }
    if (zeroGuid) {
      throw std::runtime_error("PE CodeView GUID");
    }
    if (!found) {
      identity.guid = guid;
      identity.age = age;
      found = true;
    } else if (std::memcmp(&identity.guid, &guid, sizeof(GUID)) != 0 || identity.age != age) {
      throw std::runtime_error("PE CodeView identity mismatch");
    }
  }
  if (!found) {
    throw std::runtime_error("PE CodeView missing");
  }
  identity.machine = fileHeader.Machine;
  return identity;
}

}  // namespace weasel::symbols
