#include <windows.h>
#include <dia2.h>
#include <cvconst.h>
#include <atlbase.h>
#include "PeCodeView.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <limits>
#include <cstring>

namespace {

struct ValidationFailure {
  const char* stage;
  HRESULT code;
};

inline void RequireOk(HRESULT hr, const char* stage) {
  if (hr != S_OK) {
    throw ValidationFailure{stage, hr};
  }
}

class ComApartment {
 public:
  ComApartment() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
      throw ValidationFailure{"initialize_com", hr};
    }
  }
  ~ComApartment() { CoUninitialize(); }
  ComApartment(const ComApartment&) = delete;
  ComApartment& operator=(const ComApartment&) = delete;
};

class DiaLibrary {
 public:
  explicit DiaLibrary(const std::filesystem::path& path) : module_(nullptr) {
    if (!path.is_absolute()) {
      throw ValidationFailure{"load_dia", E_INVALIDARG};
    }
    module_ = LoadLibraryExW(path.c_str(), nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                 LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module_) {
      throw ValidationFailure{"load_dia", HRESULT_FROM_WIN32(GetLastError())};
    }
  }
  ~DiaLibrary() {
    if (module_) {
      FreeLibrary(module_);
    }
  }
  HMODULE Get() const { return module_; }
  DiaLibrary(const DiaLibrary&) = delete;
  DiaLibrary& operator=(const DiaLibrary&) = delete;

 private:
  HMODULE module_;
};

constexpr uint64_t kMaxRecords = 5000000;

inline void IncrementCount(uint64_t& count) {
  if (count >= kMaxRecords) {
    throw ValidationFailure{"enumeration_limit", E_FAIL};
  }
  ++count;
}

std::vector<BYTE> ReadPeFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) {
    throw ValidationFailure{"read_pe", E_FAIL};
  }
  const std::streampos end = stream.tellg();
  if (end == std::streampos(-1)) {
    throw ValidationFailure{"read_pe", E_FAIL};
  }
  const std::streamoff length = static_cast<std::streamoff>(end);
  if (length <= 0) {
    throw ValidationFailure{"read_pe", E_FAIL};
  }
  const uint64_t length64 = static_cast<uint64_t>(length);
  constexpr uint64_t kMaxPeBytes = 512ull * 1024ull * 1024ull;
  if (length64 > kMaxPeBytes ||
      length64 >
          static_cast<uint64_t>((std::numeric_limits<size_t>::max)()) ||
      length64 >
          static_cast<uint64_t>((std::numeric_limits<std::streamsize>::max)())) {
    throw ValidationFailure{"read_pe", E_FAIL};
  }
  std::vector<BYTE> bytes(static_cast<size_t>(length64));
  stream.seekg(0, std::ios::beg);
  if (!stream) {
    throw ValidationFailure{"read_pe", E_FAIL};
  }
  stream.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  if (!stream ||
      stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
    throw ValidationFailure{"read_pe", E_FAIL};
  }
  return bytes;
}

weasel::symbols::CodeViewIdentity ReadPeIdentity(
    const std::filesystem::path& path) {
  try {
    const std::vector<BYTE> bytes = ReadPeFile(path);
    return weasel::symbols::ReadCodeViewIdentity(bytes);
  } catch (const ValidationFailure&) {
    throw;
  } catch (const std::exception&) {
    throw ValidationFailure{"read_pe", E_FAIL};
  } catch (...) {
    throw ValidationFailure{"read_pe", E_FAIL};
  }
}

struct SymbolCounts {
  uint64_t compilands = 0;
  uint64_t functions = 0;
  uint64_t lines = 0;
  uint64_t types = 0;
};

struct ValidationResult {
  weasel::symbols::CodeViewIdentity identity;
  SymbolCounts counts;
};

void ValidateSymbol(IDiaSymbol* symbol, DWORD expectedTag) {
  if (!symbol) {
    throw ValidationFailure{"symbol_record", E_FAIL};
  }
  DWORD tag = 0;
  RequireOk(symbol->get_symTag(&tag), "symbol_record");
  if (tag != expectedTag) {
    throw ValidationFailure{"symbol_record", E_FAIL};
  }
  DWORD id = 0;
  RequireOk(symbol->get_symIndexId(&id), "symbol_record");
  if (id == 0) {
    throw ValidationFailure{"symbol_record", E_FAIL};
  }
}

void CountFunctions(IDiaSymbol* compiland, SymbolCounts& counts) {
  if (!compiland) {
    throw ValidationFailure{"enumerate_functions", E_FAIL};
  }
  CComPtr<IDiaEnumSymbols> enumerator;
  const HRESULT found =
      compiland->findChildren(SymTagFunction, nullptr, nsNone, &enumerator);
  if (found == S_FALSE) {
    return;
  }
  RequireOk(found, "enumerate_functions");
  if (!enumerator) {
    throw ValidationFailure{"enumerate_functions", E_FAIL};
  }
  for (;;) {
    CComPtr<IDiaSymbol> symbol;
    ULONG fetched = 0;
    const HRESULT hr = enumerator->Next(1, &symbol, &fetched);
    if (hr == S_FALSE && fetched == 0) {
      break;
    }
    if (hr != S_OK || fetched != 1 || !symbol) {
      throw ValidationFailure{"enumerate_functions", E_FAIL};
    }
    ValidateSymbol(symbol, static_cast<DWORD>(SymTagFunction));
    IncrementCount(counts.functions);
  }
}

void CountLines(IDiaSession* session, IDiaSymbol* compiland,
                SymbolCounts& counts) {
  if (!session || !compiland) {
    throw ValidationFailure{"enumerate_lines", E_FAIL};
  }
  CComPtr<IDiaEnumSourceFiles> sourceFiles;
  const HRESULT found =
      session->findFile(compiland, nullptr, nsNone, &sourceFiles);
  if (found == S_FALSE) {
    return;
  }
  RequireOk(found, "enumerate_lines");
  if (!sourceFiles) {
    throw ValidationFailure{"enumerate_lines", E_FAIL};
  }
  for (;;) {
    CComPtr<IDiaSourceFile> sourceFile;
    ULONG fetched = 0;
    const HRESULT hr = sourceFiles->Next(1, &sourceFile, &fetched);
    if (hr == S_FALSE && fetched == 0) {
      break;
    }
    if (hr != S_OK || fetched != 1 || !sourceFile) {
      throw ValidationFailure{"enumerate_lines", E_FAIL};
    }
    DWORD uniqueId = 0;
    RequireOk(sourceFile->get_uniqueId(&uniqueId), "enumerate_lines");

    CComPtr<IDiaEnumLineNumbers> lines;
    const HRESULT lineResult =
        session->findLines(compiland, sourceFile, &lines);
    if (lineResult == S_FALSE) {
      continue;
    }
    RequireOk(lineResult, "enumerate_lines");
    if (!lines) {
      throw ValidationFailure{"enumerate_lines", E_FAIL};
    }
    for (;;) {
      CComPtr<IDiaLineNumber> line;
      ULONG lineFetched = 0;
      const HRESULT lineHr = lines->Next(1, &line, &lineFetched);
      if (lineHr == S_FALSE && lineFetched == 0) {
        break;
      }
      if (lineHr != S_OK || lineFetched != 1 || !line) {
        throw ValidationFailure{"enumerate_lines", E_FAIL};
      }
      DWORD lineNumber = 0;
      DWORD rva = 0;
      DWORD sourceFileId = 0;
      RequireOk(line->get_lineNumber(&lineNumber), "line_record");
      RequireOk(line->get_relativeVirtualAddress(&rva), "line_record");
      RequireOk(line->get_sourceFileId(&sourceFileId), "line_record");
      if (lineNumber == 0 || sourceFileId != uniqueId) {
        throw ValidationFailure{"line_record", E_FAIL};
      }
      IncrementCount(counts.lines);
    }
  }
}

void ValidateSession(IDiaSession* session, SymbolCounts& counts) {
  if (!session) {
    throw ValidationFailure{"open_session", E_FAIL};
  }
  CComPtr<IDiaSymbol> global;
  RequireOk(session->get_globalScope(&global), "open_session");
  if (!global) {
    throw ValidationFailure{"open_session", E_FAIL};
  }

  CComPtr<IDiaEnumSymbols> compilands;
  const HRESULT compilandResult =
      global->findChildren(SymTagCompiland, nullptr, nsNone, &compilands);
  if (compilandResult != S_FALSE) {
    RequireOk(compilandResult, "enumerate_symbols");
    if (!compilands) {
      throw ValidationFailure{"enumerate_symbols", E_FAIL};
    }
    for (;;) {
      CComPtr<IDiaSymbol> compiland;
      ULONG fetched = 0;
      const HRESULT hr = compilands->Next(1, &compiland, &fetched);
      if (hr == S_FALSE && fetched == 0) {
        break;
      }
      if (hr != S_OK || fetched != 1 || !compiland) {
        throw ValidationFailure{"enumerate_symbols", E_FAIL};
      }
      ValidateSymbol(compiland, static_cast<DWORD>(SymTagCompiland));
      IncrementCount(counts.compilands);
      CountFunctions(compiland, counts);
      CountLines(session, compiland, counts);
    }
  }

  CComPtr<IDiaEnumSymbols> types;
  const HRESULT typeResult =
      global->findChildren(SymTagUDT, nullptr, nsNone, &types);
  if (typeResult != S_FALSE) {
    RequireOk(typeResult, "enumerate_symbols");
    if (!types) {
      throw ValidationFailure{"enumerate_symbols", E_FAIL};
    }
    for (;;) {
      CComPtr<IDiaSymbol> type;
      ULONG fetched = 0;
      const HRESULT hr = types->Next(1, &type, &fetched);
      if (hr == S_FALSE && fetched == 0) {
        break;
      }
      if (hr != S_OK || fetched != 1 || !type) {
        throw ValidationFailure{"enumerate_symbols", E_FAIL};
      }
      ValidateSymbol(type, static_cast<DWORD>(SymTagUDT));
      ULONGLONG length = 0;
      RequireOk(type->get_length(&length), "symbol_record");
      IncrementCount(counts.types);
    }
  }

  if (counts.compilands == 0 || counts.functions == 0 || counts.lines == 0 ||
      counts.types == 0) {
    throw ValidationFailure{"empty_symbols", E_FAIL};
  }
}

ValidationResult RunValidation(const std::filesystem::path& binaryPath,
                               const std::filesystem::path& pdbPath,
                               const std::filesystem::path& diaDllPath) {
  weasel::symbols::CodeViewIdentity identity = ReadPeIdentity(binaryPath);
  SymbolCounts counts;

  ComApartment apartment;
  DiaLibrary dia(diaDllPath);
  {
    CComPtr<IClassFactory> factory;
    FARPROC proc = GetProcAddress(dia.Get(), "DllGetClassObject");
    if (!proc) {
      const DWORD error = GetLastError();
      throw ValidationFailure{"create_source",
                              error ? HRESULT_FROM_WIN32(error) : E_FAIL};
    }
    using DllGetClassObjectFn =
        HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, LPVOID*);
    static_assert(sizeof(DllGetClassObjectFn) == sizeof(FARPROC),
                  "DllGetClassObject function pointer size mismatch");
    DllGetClassObjectFn getClassObject = nullptr;
    std::memcpy(&getClassObject, &proc, sizeof(getClassObject));
    if (!getClassObject) {
      throw ValidationFailure{"create_source", E_FAIL};
    }
    RequireOk(getClassObject(CLSID_DiaSource, IID_IClassFactory,
                             reinterpret_cast<void**>(&factory)),
              "create_source");
    if (!factory) {
      throw ValidationFailure{"create_source", E_FAIL};
    }

    CComPtr<IDiaDataSource> source;
    RequireOk(factory->CreateInstance(
                  nullptr, __uuidof(IDiaDataSource),
                  reinterpret_cast<void**>(&source)),
              "create_source");
    if (!source) {
      throw ValidationFailure{"create_source", E_FAIL};
    }

    CComPtr<IDiaSession> session;
    RequireOk(source->loadAndValidateDataFromPdb(
                  pdbPath.c_str(), &identity.guid, 0, identity.age),
              "load_validate_pdb");
    RequireOk(source->openSession(&session), "open_session");
    if (!session) {
      throw ValidationFailure{"open_session", E_FAIL};
    }

    ValidateSession(session, counts);
  }
  return ValidationResult{identity, counts};
}

void PrintSuccess(const ValidationResult& result) {
  wchar_t guid[40] = {};
  if (StringFromGUID2(result.identity.guid, guid, 40) <= 0) {
    throw ValidationFailure{"unexpected", E_FAIL};
  }
  std::wcout << L"{\"schemaVersion\":1,\"ok\":true,\"machine\":"
             << result.identity.machine << L",\"age\":" << result.identity.age
             << L",\"guid\":\"" << guid << L"\",\"compilands\":"
             << result.counts.compilands << L",\"functions\":"
             << result.counts.functions << L",\"lines\":"
             << result.counts.lines << L",\"types\":" << result.counts.types
             << L"}" << std::endl;
}

void PrintFailure(const char* stage, HRESULT hr) {
  std::wcout << L"{\"schemaVersion\":1,\"ok\":false,\"stage\":\"" << stage
             << L"\",\"hresult\":" << static_cast<DWORD>(hr) << L"}"
             << std::endl;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 4) {
    std::cerr << "usage: validate_native_symbols <binary> <pdb> "
                 "<absolute-dia-dll>\n";
    return 2;
  }
  try {
    const ValidationResult result = RunValidation(argv[1], argv[2], argv[3]);
    PrintSuccess(result);
    return 0;
  } catch (const ValidationFailure& failure) {
    PrintFailure(failure.stage, failure.code);
    return 1;
  } catch (const std::exception&) {
    PrintFailure("unexpected", E_FAIL);
    return 1;
  } catch (...) {
    PrintFailure("unexpected", E_FAIL);
    return 1;
  }
}
