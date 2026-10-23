#include <cstddef>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "PeCodeView.h"

namespace {

const GUID kExpectedGuid = {0x11223344u, 0x5566u, 0x7788u,
                            {0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00}};
const GUID kOtherGuid = {0x01020304u, 0x0506u, 0x0708u,
                         {0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10}};

template <class T>
void PutPePod(std::vector<BYTE>& bytes, size_t offset, const T& value) {
  if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
    throw std::runtime_error("fixture range");
  }
  std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

std::vector<BYTE> MakePeFixture(bool is64) {
  static_assert(sizeof(IMAGE_OPTIONAL_HEADER32) <= 0xFFFF, "optional header 32 too large");
  static_assert(sizeof(IMAGE_OPTIONAL_HEADER64) <= 0xFFFF, "optional header 64 too large");
  static_assert(sizeof(IMAGE_DEBUG_DIRECTORY) <= 0xFFFFFFFFu, "debug directory too large");

  std::vector<BYTE> bytes(0x600, 0);

  IMAGE_DOS_HEADER dos{};
  dos.e_magic = static_cast<WORD>(IMAGE_DOS_SIGNATURE);
  dos.e_lfanew = 0x80;
  PutPePod(bytes, 0, dos);

  const DWORD ntSignature = static_cast<DWORD>(IMAGE_NT_SIGNATURE);
  PutPePod(bytes, 0x80, ntSignature);

  IMAGE_FILE_HEADER fileHeader{};
  fileHeader.Machine = is64 ? static_cast<WORD>(IMAGE_FILE_MACHINE_AMD64)
                            : static_cast<WORD>(IMAGE_FILE_MACHINE_I386);
  fileHeader.NumberOfSections = 1;
  fileHeader.SizeOfOptionalHeader =
      is64 ? static_cast<WORD>(sizeof(IMAGE_OPTIONAL_HEADER64))
           : static_cast<WORD>(sizeof(IMAGE_OPTIONAL_HEADER32));
  PutPePod(bytes, 0x84, fileHeader);

  const size_t optionalOffset = 0x84u + sizeof(IMAGE_FILE_HEADER);
  size_t sectionOffset = 0;
  if (is64) {
    IMAGE_OPTIONAL_HEADER64 optional{};
    optional.Magic = static_cast<WORD>(IMAGE_NT_OPTIONAL_HDR64_MAGIC);
    optional.SizeOfHeaders = 0x400u;
    optional.NumberOfRvaAndSizes = static_cast<DWORD>(IMAGE_NUMBEROF_DIRECTORY_ENTRIES);
    optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress = 0x1000u;
    optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size =
        static_cast<DWORD>(sizeof(IMAGE_DEBUG_DIRECTORY));
    PutPePod(bytes, optionalOffset, optional);
    sectionOffset = optionalOffset + sizeof(IMAGE_OPTIONAL_HEADER64);
  } else {
    IMAGE_OPTIONAL_HEADER32 optional{};
    optional.Magic = static_cast<WORD>(IMAGE_NT_OPTIONAL_HDR32_MAGIC);
    optional.SizeOfHeaders = 0x400u;
    optional.NumberOfRvaAndSizes = static_cast<DWORD>(IMAGE_NUMBEROF_DIRECTORY_ENTRIES);
    optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress = 0x1000u;
    optional.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size =
        static_cast<DWORD>(sizeof(IMAGE_DEBUG_DIRECTORY));
    PutPePod(bytes, optionalOffset, optional);
    sectionOffset = optionalOffset + sizeof(IMAGE_OPTIONAL_HEADER32);
  }

  IMAGE_SECTION_HEADER section{};
  std::memcpy(section.Name, ".rdata", 7);
  section.VirtualAddress = 0x1000u;
  section.SizeOfRawData = 0x200u;
  section.PointerToRawData = 0x400u;
  PutPePod(bytes, sectionOffset, section);

  IMAGE_DEBUG_DIRECTORY debug{};
  debug.Type = static_cast<DWORD>(IMAGE_DEBUG_TYPE_CODEVIEW);
  debug.SizeOfData = 24u;
  debug.AddressOfRawData = 0x1100u;
  debug.PointerToRawData = 0x500u;
  PutPePod(bytes, 0x400, debug);

  std::memcpy(bytes.data() + 0x500, "RSDS", 4);
  PutPePod(bytes, 0x504, kExpectedGuid);
  const DWORD age = 3u;
  PutPePod(bytes, 0x514, age);
  return bytes;
}

bool ExpectRejected(const char* name, const std::vector<BYTE>& bytes) {
  try {
    (void)weasel::symbols::ReadCodeViewIdentity(bytes);
  } catch (const std::runtime_error&) {
    std::cout << "PASS " << name << "\n";
    return true;
  }
  std::cerr << "FAIL " << name << ": accepted invalid PE\n";
  return false;
}

}  // namespace

int main() {
  static_assert(sizeof(IMAGE_OPTIONAL_HEADER32) <= 0xFFFF, "optional header 32 too large");
  static_assert(sizeof(IMAGE_OPTIONAL_HEADER64) <= 0xFFFF, "optional header 64 too large");
  static_assert(sizeof(IMAGE_DEBUG_DIRECTORY) <= 0xFFFFFFFFu, "debug directory too large");
  static_assert(sizeof(IMAGE_DEBUG_DIRECTORY) * 2 <= 0xFFFFFFFFu,
                "two debug directories too large");
  static_assert(sizeof(IMAGE_DEBUG_DIRECTORY) + 1 <= 0xFFFFFFFFu,
                "debug directory plus one too large");

  try {
    const std::vector<BYTE> pe32 = MakePeFixture(false);
    const std::vector<BYTE> pe64 = MakePeFixture(true);
    const weasel::symbols::CodeViewIdentity id32 = weasel::symbols::ReadCodeViewIdentity(pe32);
    const weasel::symbols::CodeViewIdentity id64 = weasel::symbols::ReadCodeViewIdentity(pe64);
    if (std::memcmp(&id32.guid, &kExpectedGuid, sizeof(GUID)) != 0 ||
        std::memcmp(&id64.guid, &kExpectedGuid, sizeof(GUID)) != 0) {
      throw std::runtime_error("normal fixture GUID mismatch");
    }
    if (id32.age != 3u || id64.age != 3u) {
      throw std::runtime_error("normal fixture age mismatch");
    }
    if (id32.machine != static_cast<WORD>(IMAGE_FILE_MACHINE_I386) ||
        id64.machine != static_cast<WORD>(IMAGE_FILE_MACHINE_AMD64)) {
      throw std::runtime_error("normal fixture machine mismatch");
    }
    if (std::memcmp(&id32.guid, &id64.guid, sizeof(GUID)) != 0 || id32.age != id64.age) {
      throw std::runtime_error("32/64 identity mismatch");
    }

    std::vector<BYTE> headerMapped = MakePeFixture(false);
    headerMapped.resize(0x1300, 0);
    const size_t optionalOffset = 0x84u + sizeof(IMAGE_FILE_HEADER);
    IMAGE_OPTIONAL_HEADER32 optional =
        weasel::symbols::ReadPePod<IMAGE_OPTIONAL_HEADER32>(headerMapped, optionalOffset);
    optional.SizeOfHeaders = 0x1200u;
    PutPePod(headerMapped, optionalOffset, optional);
    IMAGE_DEBUG_DIRECTORY headerDebug{};
    headerDebug.Type = static_cast<DWORD>(IMAGE_DEBUG_TYPE_CODEVIEW);
    headerDebug.SizeOfData = 24u;
    headerDebug.AddressOfRawData = 0x1100u;
    headerDebug.PointerToRawData = 0x1100u;
    PutPePod(headerMapped, 0x1000, headerDebug);
    std::memcpy(headerMapped.data() + 0x1100, "RSDS", 4);
    PutPePod(headerMapped, 0x1104, kExpectedGuid);
    const DWORD headerAge = 3u;
    PutPePod(headerMapped, 0x1114, headerAge);
    const weasel::symbols::CodeViewIdentity headerId =
        weasel::symbols::ReadCodeViewIdentity(headerMapped);
    if (std::memcmp(&headerId.guid, &kExpectedGuid, sizeof(GUID)) != 0 ||
        headerId.age != 3u ||
        headerId.machine != static_cast<WORD>(IMAGE_FILE_MACHINE_I386)) {
      throw std::runtime_error("header RVA mapping mismatch");
    }

    std::vector<BYTE> duplicate = MakePeFixture(false);
    IMAGE_OPTIONAL_HEADER32 duplicateOptional =
        weasel::symbols::ReadPePod<IMAGE_OPTIONAL_HEADER32>(duplicate, 0x98);
    duplicateOptional.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size =
        static_cast<DWORD>(sizeof(IMAGE_DEBUG_DIRECTORY) * 2);
    PutPePod(duplicate, 0x98, duplicateOptional);
    IMAGE_DEBUG_DIRECTORY duplicateEntry{};
    duplicateEntry.Type = static_cast<DWORD>(IMAGE_DEBUG_TYPE_CODEVIEW);
    duplicateEntry.SizeOfData = 24u;
    duplicateEntry.AddressOfRawData = 0x1100u;
    duplicateEntry.PointerToRawData = 0x500u;
    PutPePod(duplicate, 0x400 + sizeof(IMAGE_DEBUG_DIRECTORY), duplicateEntry);
    const weasel::symbols::CodeViewIdentity duplicateId =
        weasel::symbols::ReadCodeViewIdentity(duplicate);
    if (std::memcmp(&duplicateId.guid, &kExpectedGuid, sizeof(GUID)) != 0 ||
        duplicateId.age != 3u) {
      throw std::runtime_error("consistent duplicate RSDS mismatch");
    }

    bool ok = true;

    {
      std::vector<BYTE> legacyRecord = duplicate;
      IMAGE_DEBUG_DIRECTORY legacyEntry{};
      legacyEntry.Type = static_cast<DWORD>(IMAGE_DEBUG_TYPE_CODEVIEW);
      legacyEntry.SizeOfData = 16u;
      legacyEntry.AddressOfRawData = 0x1120u;
      legacyEntry.PointerToRawData = 0x520u;
      PutPePod(legacyRecord, 0x400 + sizeof(IMAGE_DEBUG_DIRECTORY), legacyEntry);
      std::memcpy(legacyRecord.data() + 0x520, "NB10", 4);
      const weasel::symbols::CodeViewIdentity legacyId =
          weasel::symbols::ReadCodeViewIdentity(legacyRecord);
      if (std::memcmp(&legacyId.guid, &kExpectedGuid, sizeof(GUID)) != 0 ||
          legacyId.age != 3u) {
        throw std::runtime_error("legacy NB10 record compatibility mismatch");
      }
    }
    {
      std::vector<BYTE> truncatedRecord = duplicate;
      IMAGE_DEBUG_DIRECTORY truncatedEntry{};
      truncatedEntry.Type = static_cast<DWORD>(IMAGE_DEBUG_TYPE_CODEVIEW);
      truncatedEntry.SizeOfData = 23u;
      truncatedEntry.AddressOfRawData = 0x1120u;
      truncatedEntry.PointerToRawData = 0x520u;
      PutPePod(truncatedRecord, 0x400 + sizeof(IMAGE_DEBUG_DIRECTORY), truncatedEntry);
      std::memcpy(truncatedRecord.data() + 0x520, "RSDS", 4);
      ok = ExpectRejected("truncated CodeView record", truncatedRecord) && ok;
    }
    {
      std::vector<BYTE> empty;
      ok = ExpectRejected("empty", empty) && ok;
    }
    {
      std::vector<BYTE> truncated(sizeof(IMAGE_DOS_HEADER) - 1, 0);
      ok = ExpectRejected("truncated DOS", truncated) && ok;
    }
    {
      std::vector<BYTE> badMz = MakePeFixture(false);
      IMAGE_DOS_HEADER dos = weasel::symbols::ReadPePod<IMAGE_DOS_HEADER>(badMz, 0);
      dos.e_magic = 0;
      PutPePod(badMz, 0, dos);
      ok = ExpectRejected("bad MZ", badMz) && ok;
    }
    {
      std::vector<BYTE> negative = MakePeFixture(false);
      IMAGE_DOS_HEADER dos = weasel::symbols::ReadPePod<IMAGE_DOS_HEADER>(negative, 0);
      dos.e_lfanew = -1;
      PutPePod(negative, 0, dos);
      ok = ExpectRejected("negative e_lfanew", negative) && ok;
    }
    {
      std::vector<BYTE> missingNt = MakePeFixture(false);
      const DWORD zero = 0u;
      PutPePod(missingNt, 0x80, zero);
      ok = ExpectRejected("missing NT signature", missingNt) && ok;
    }
    {
      std::vector<BYTE> incomplete = MakePeFixture(false);
      IMAGE_FILE_HEADER fh =
          weasel::symbols::ReadPePod<IMAGE_FILE_HEADER>(incomplete, 0x84);
      fh.SizeOfOptionalHeader = static_cast<WORD>(sizeof(IMAGE_OPTIONAL_HEADER32) - 1);
      PutPePod(incomplete, 0x84, fh);
      ok = ExpectRejected("incomplete optional header", incomplete) && ok;
    }
    {
      std::vector<BYTE> nonMultiple = MakePeFixture(false);
      IMAGE_OPTIONAL_HEADER32 nonMultipleOptional =
          weasel::symbols::ReadPePod<IMAGE_OPTIONAL_HEADER32>(nonMultiple, 0x98);
      nonMultipleOptional.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size =
          static_cast<DWORD>(sizeof(IMAGE_DEBUG_DIRECTORY) + 1);
      PutPePod(nonMultiple, 0x98, nonMultipleOptional);
      ok = ExpectRejected("directory size non-multiple", nonMultiple) && ok;
    }
    {
      std::vector<BYTE> unmapped = MakePeFixture(false);
      IMAGE_OPTIONAL_HEADER32 unmappedOptional =
          weasel::symbols::ReadPePod<IMAGE_OPTIONAL_HEADER32>(unmapped, 0x98);
      unmappedOptional.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress = 0x2000u;
      PutPePod(unmapped, 0x98, unmappedOptional);
      ok = ExpectRejected("cannot map RVA", unmapped) && ok;
    }
    {
      std::vector<BYTE> outOfBounds = MakePeFixture(false);
      IMAGE_DEBUG_DIRECTORY dd =
          weasel::symbols::ReadPePod<IMAGE_DEBUG_DIRECTORY>(outOfBounds, 0x400);
      dd.PointerToRawData = 0x5F0u;
      PutPePod(outOfBounds, 0x400, dd);
      ok = ExpectRejected("debug raw data out of bounds", outOfBounds) && ok;
    }
    {
      std::vector<BYTE> noRsds = MakePeFixture(false);
      std::memcpy(noRsds.data() + 0x500, "NB10", 4);
      ok = ExpectRejected("no RSDS", noRsds) && ok;
    }
    {
      std::vector<BYTE> zeroGuid = MakePeFixture(false);
      std::memset(zeroGuid.data() + 0x504, 0, sizeof(GUID));
      ok = ExpectRejected("zero GUID", zeroGuid) && ok;
    }
    {
      std::vector<BYTE> zeroAge = MakePeFixture(false);
      const DWORD zero = 0u;
      PutPePod(zeroAge, 0x514, zero);
      ok = ExpectRejected("zero age", zeroAge) && ok;
    }
    {
      std::vector<BYTE> inconsistent = MakePeFixture(false);
      IMAGE_OPTIONAL_HEADER32 inconsistentOptional =
          weasel::symbols::ReadPePod<IMAGE_OPTIONAL_HEADER32>(inconsistent, 0x98);
      inconsistentOptional.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size =
          static_cast<DWORD>(sizeof(IMAGE_DEBUG_DIRECTORY) * 2);
      PutPePod(inconsistent, 0x98, inconsistentOptional);
      IMAGE_DEBUG_DIRECTORY second{};
      second.Type = static_cast<DWORD>(IMAGE_DEBUG_TYPE_CODEVIEW);
      second.SizeOfData = 24u;
      second.AddressOfRawData = 0x1120u;
      second.PointerToRawData = 0x520u;
      PutPePod(inconsistent, 0x400 + sizeof(IMAGE_DEBUG_DIRECTORY), second);
      std::memcpy(inconsistent.data() + 0x520, "RSDS", 4);
      PutPePod(inconsistent, 0x524, kOtherGuid);
      const DWORD otherAge = 4u;
      PutPePod(inconsistent, 0x534, otherAge);
      ok = ExpectRejected("two inconsistent RSDS", inconsistent) && ok;
    }

    if (!ok) {
      return 1;
    }
    std::cout << "PASS PE CodeView parser\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << "\n";
    return 1;
  } catch (...) {
    std::cerr << "FAIL: unknown exception\n";
    return 1;
  }
}
