#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <cstdio>
#include <iostream>
#include <string>
// These are exact production classification and stream-setup statements.
#include "console_options_under_test.inc"
static void DetachGuiConsole() {}
int wmain(int argc, wchar_t* argv[]) {
#include "console_setup_under_test.inc"
  std::wcout << L"工作词典\n";
  if (argc > 3) std::wcout << argv[3] << L'\n';
  std::wcerr << L"诊断信息\n";
  return 0;
}
