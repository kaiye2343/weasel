// Launch only a supplied isolated fixture. No attach, injection or input hooks.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <WeaselPerformance.h>

int wmain(int argc, wchar_t** argv) {
  if (argc != 3) return 2;
  wchar_t* end = nullptr;
  const unsigned long long probeRva = std::wcstoull(argv[2], &end, 16);
  if (!end || *end || !probeRva) return 2;
  std::wstring command = L"\"" + std::wstring(argv[1]) + L"\"";
  STARTUPINFOW startup{}; startup.cb = sizeof(startup);
  PROCESS_INFORMATION child{};
  if (!CreateProcessW(argv[1], command.data(), nullptr, nullptr, FALSE,
                      DEBUG_ONLY_THIS_PROCESS | CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child)) {
    std::fprintf(stderr, "[passive-debug] launch error=%lu\n", GetLastError()); return 3;
  }
  // Duplicate the returned handles: debug-event handles may be closed by the
  // system on EXIT_PROCESS. This owned handle remains valid for cleanup.
  HANDLE ownedProcess = nullptr;
  if (!DuplicateHandle(GetCurrentProcess(), child.hProcess, GetCurrentProcess(), &ownedProcess,
                       0, FALSE, DUPLICATE_SAME_ACCESS)) return 4;
  DWORD64 imageBase = 0;
  int result = 5;
  const ULONGLONG deadline = GetTickCount64() + 20000;
  while (GetTickCount64() < deadline) {
    DEBUG_EVENT event{};
    if (!WaitForDebugEvent(&event, 1000)) {
      if (GetLastError() == ERROR_SEM_TIMEOUT) continue;
      break;
    }
    DWORD continuation = DBG_CONTINUE;
    if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
      imageBase = reinterpret_cast<ULONG_PTR>(event.u.CreateProcessInfo.lpBaseOfImage);
      std::fprintf(stderr, "[passive-debug] image-base=0x%llx probe-rva=0x%llx\n", imageBase, probeRva);
      if (event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile);
    } else if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) {
      if (event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile);
    } else if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
      const auto& exception = event.u.Exception.ExceptionRecord;
      if (exception.ExceptionCode != EXCEPTION_BREAKPOINT) continuation = DBG_EXCEPTION_NOT_HANDLED;
      if (exception.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && !event.u.Exception.dwFirstChance) {
        std::fprintf(stderr, "[passive-debug] code=0x%08lx instruction=%p operation=%llu target=0x%llx\n",
          exception.ExceptionCode, exception.ExceptionAddress,
          static_cast<unsigned long long>(exception.ExceptionInformation[0]),
          static_cast<unsigned long long>(exception.ExceptionInformation[1]));
        unsigned char storage[sizeof(weasel::performance::Probe)]{};
        SIZE_T bytes = 0;
        if (ReadProcessMemory(ownedProcess, reinterpret_cast<const void*>(static_cast<ULONG_PTR>(imageBase + probeRva)),
                              storage, sizeof(storage), &bytes) && bytes == sizeof(storage)) {
          unsigned nonzero = 0;
          for (const auto value : storage) if (value) ++nonzero;
          std::fprintf(stderr, "[passive-debug] probe-bytes=%zu nonzero-bytes=%u\n", bytes, nonzero);
        }
        HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, event.dwThreadId);
        CONTEXT context{}; context.ContextFlags = CONTEXT_FULL;
        if (thread && GetThreadContext(thread, &context)) {
          STACKFRAME64 frame{};
#if defined(_M_X64)
          const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
          frame.AddrPC.Offset=context.Rip; frame.AddrStack.Offset=context.Rsp; frame.AddrFrame.Offset=context.Rbp;
          std::fprintf(stderr,"[passive-debug] rcx=0x%llx rdx=0x%llx rsp=0x%llx\n",context.Rcx,context.Rdx,context.Rsp);
#else
          const DWORD machine = IMAGE_FILE_MACHINE_I386;
          frame.AddrPC.Offset=context.Eip; frame.AddrStack.Offset=context.Esp; frame.AddrFrame.Offset=context.Ebp;
#endif
          frame.AddrPC.Mode=frame.AddrStack.Mode=frame.AddrFrame.Mode=AddrModeFlat;
          ULONG_PTR top = 0;
          if (ReadProcessMemory(ownedProcess,reinterpret_cast<const void*>(static_cast<ULONG_PTR>(frame.AddrStack.Offset)),&top,sizeof(top),&bytes) && bytes==sizeof(top))
            std::fprintf(stderr,"[passive-debug] stack-top=0x%llx possible-return-rva=0x%llx\n",static_cast<unsigned long long>(top),static_cast<unsigned long long>(top-imageBase));
          if (SymInitialize(ownedProcess,nullptr,TRUE)) {
            for(int index=0;index<24;++index) {
              if(!StackWalk64(machine,ownedProcess,thread,&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr)||!frame.AddrPC.Offset)break;
              std::fprintf(stderr,"[passive-debug] frame-pc=0x%llx rva=0x%llx\n",frame.AddrPC.Offset,frame.AddrPC.Offset-imageBase);
            }
            SymCleanup(ownedProcess);
          }
        }
        if(thread)CloseHandle(thread);
        result = 0;
      }
    } else if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
      std::fprintf(stderr,"[passive-debug] target-exit=0x%08lx\n",event.u.ExitProcess.dwExitCode);
      ContinueDebugEvent(event.dwProcessId,event.dwThreadId,DBG_CONTINUE);
      break;
    }
    if(!ContinueDebugEvent(event.dwProcessId,event.dwThreadId,continuation))break;
  }
  if(WaitForSingleObject(ownedProcess,0)!=WAIT_OBJECT_0) {
    TerminateProcess(ownedProcess,5); WaitForSingleObject(ownedProcess,5000);
  }
  CloseHandle(ownedProcess);
  return result;
}
