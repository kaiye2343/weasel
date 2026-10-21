// Use the same extracted production CLI, replacing the fake API table with a
// real DLL table. Mutex/IPC stay fake so only the fresh fixture can be touched.
#define main DictionaryMockMain
#include "deployer_dictionary_test.cpp"
#undef main
#include <fstream>
#include <fcntl.h>
#include <io.h>
int main(int argc, char** argv) try {
  _setmode(_fileno(stdout), _O_U8TEXT);
  Require(argc == 3, "provide DLL and fresh isolated root");
  const auto dll = std::filesystem::absolute(std::filesystem::u8path(argv[1]));
  const auto root = std::filesystem::absolute(std::filesystem::u8path(argv[2]));
  Require(!std::filesystem::exists(root), "fixture must not already exist");
  std::filesystem::create_directories(root);
  HMODULE library = LoadLibraryW(dll.c_str());
  Require(library != nullptr, "load pinned librime failed");
  auto get_api = reinterpret_cast<RimeApi* (*)()>(GetProcAddress(library, "rime_get_api"));
  Require(get_api != nullptr, "missing API export");
  rime = *get_api();
  const std::string directory = root.u8string();
  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = traits.user_data_dir = directory.c_str();
  traits.log_dir = directory.c_str();
  traits.app_name = "rime.deployer_cli_test";
  traits.min_log_level = 2;
  std::ofstream(root / "installation.yaml") << "installation_id: cli-test\n";
  rime.setup(&traits);
  rime.deployer_initialize(nullptr);
  const auto input = root / L"中文 词库.tsv";
  const auto exported = root / L"导出 词库.tsv";
  std::ofstream(input, std::ios::binary) << u8"测试词\tce shi ci\t10\n";
  Configurator c;
  Require(c.DictManagement({L"import", L"work", input.wstring()}) == 0, "real import failed");
  Require(c.DictManagement({L"export", L"work", exported.wstring()}) == 0, "real export failed");
  std::ifstream read(exported, std::ios::binary);
  std::string text{std::istreambuf_iterator<char>(read), {}};
  Require(text.find(u8"测试词") != std::string::npos, "real roundtrip lost phrase");
  Require(c.DictManagement({L"backup", L"work"}) == 0, "real backup failed");
  auto snapshot = root / "sync" / "cli-test" / "work.userdb.txt";
  Require(std::filesystem::exists(snapshot), "backup output not at reported path");
  Require(c.DictManagement({L"restore", snapshot.wstring()}) == 0, "real restore failed");
  Require(c.DictManagement({L"list"}) == 0, "real list failed");
  Require(c.DictManagement({L"import", L"work", (root/L"missing.tsv").wstring()}) == 1, "missing import reported success");
  Require(c.DictManagement({L"restore", (root/L"missing.userdb.txt").wstring()}) == 1, "missing restore reported success");
  rime.finalize();
  std::wcout << L"PASS production CLI with real librime: isolated import/export/backup/restore/list and missing files\n";
  return 0;
} catch (const std::exception& error) {
  std::clog << "FAIL " << error.what() << '\n'; return 1;
}
