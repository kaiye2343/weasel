#pragma once
// Isolated test diagnostics only. Never included by a product endpoint.
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>

inline void WriteFaultAddress(HANDLE process, DWORD64 address) {
  alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
  auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
  symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol->MaxNameLen = MAX_SYM_NAME;
  DWORD64 displacement = 0;
  std::fprintf(stderr, "[response-fault] pc=0x%llx", static_cast<unsigned long long>(address));
  if (SymFromAddr(process, address, &displacement, symbol))
    std::fprintf(stderr, " %s+0x%llx", symbol->Name, static_cast<unsigned long long>(displacement));
  IMAGEHLP_LINE64 line{};
  line.SizeOfStruct = sizeof(line);
  DWORD lineDisplacement = 0;
  if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line))
    std::fprintf(stderr, " %s:%lu", line.FileName, line.LineNumber);
  IMAGEHLP_MODULE64 module{};
  module.SizeOfStruct = sizeof(module);
  if (SymGetModuleInfo64(process, address, &module))
    std::fprintf(stderr, " module=%s timestamp=%lu size=%lu", module.ModuleName, module.TimeDateStamp, module.ImageSize);
  std::fputc('\n', stderr);
}

inline LONG WINAPI ReportNativeFault(EXCEPTION_POINTERS* details) {
  const auto* record = details->ExceptionRecord;
  std::fprintf(stderr, "[response-fault] code=0x%08lx instruction=%p\n", record->ExceptionCode, record->ExceptionAddress);
  if (record->NumberParameters >= 2)
    std::fprintf(stderr, "[response-fault] operation=%llu target=0x%llx\n",
      static_cast<unsigned long long>(record->ExceptionInformation[0]),
      static_cast<unsigned long long>(record->ExceptionInformation[1]));
  CONTEXT context = *details->ContextRecord;
  STACKFRAME64 frame{};
#if defined(_M_X64)
  const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
  frame.AddrPC.Offset = context.Rip;
  frame.AddrStack.Offset = context.Rsp;
  frame.AddrFrame.Offset = context.Rbp;
  std::fprintf(stderr, "[response-fault] rcx=0x%llx rdx=0x%llx r8=0x%llx r9=0x%llx rsp=0x%llx rbp=0x%llx\n",
    context.Rcx, context.Rdx, context.R8, context.R9, context.Rsp, context.Rbp);
#elif defined(_M_IX86)
  const DWORD machine = IMAGE_FILE_MACHINE_I386;
  frame.AddrPC.Offset = context.Eip;
  frame.AddrStack.Offset = context.Esp;
  frame.AddrFrame.Offset = context.Ebp;
  std::fprintf(stderr, "[response-fault] eax=0x%lx ecx=0x%lx edx=0x%lx esp=0x%lx ebp=0x%lx\n",
    context.Eax, context.Ecx, context.Edx, context.Esp, context.Ebp);
#else
#error Unsupported isolated response test architecture
#endif
  frame.AddrPC.Mode = frame.AddrStack.Mode = frame.AddrFrame.Mode = AddrModeFlat;
  const HANDLE process = GetCurrentProcess();
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  if (!SymInitialize(process, nullptr, TRUE)) {
    std::fprintf(stderr, "[response-fault] SymInitialize error=%lu\n", GetLastError());
  } else {
    WriteFaultAddress(process, frame.AddrPC.Offset);
    // If the fault is an indirect call to null, preserve the actual stack word
    // as a separate observation, not as a fabricated successful unwind.
    if (frame.AddrPC.Offset == 0) {
      ULONG_PTR returnAddress = 0;
      SIZE_T bytes = 0;
      if (ReadProcessMemory(process, reinterpret_cast<const void*>(static_cast<ULONG_PTR>(frame.AddrStack.Offset)),
                            &returnAddress, sizeof(returnAddress), &bytes) && bytes == sizeof(returnAddress)) {
        std::fputs("[response-fault] stack-top possible return address:\n", stderr);
        WriteFaultAddress(process, static_cast<DWORD64>(returnAddress));
      }
    }
    for (int index = 0; index < 32; ++index) {
      if (!StackWalk64(machine, process, GetCurrentThread(), &frame, &context,
                       nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
          frame.AddrPC.Offset == 0) break;
      WriteFaultAddress(process, frame.AddrPC.Offset);
    }
    SymCleanup(process);
  }
  std::fflush(stderr);
  // Keep the failing process and exit code failing; never resume the fault.
  return EXCEPTION_EXECUTE_HANDLER;
}
