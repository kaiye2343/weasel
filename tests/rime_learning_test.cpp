#include <rime_api.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
static RimeApi* api;
static void Require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
static void Write(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary); out << text;
  Require((bool)out, "fixture write failed");
}
static void Fixture(const fs::path& root, const char* id) {
  Write(root / "installation.yaml", std::string("installation_id: ") + id + "\n");
  Write(root / "default.yaml", "config_version: '1.0'\nschema_list:\n  - schema: work\n  - schema: daily\nmenu:\n  page_size: 5\n");
  Write(root / "base.dict.yaml", u8"---\nname: base\nversion: '1.0'\nsort: by_weight\n...\n你\tni\t100\n好\thao\t100\n");
  for (const char* scene : {"work", "daily"}) {
    Write(root / (std::string(scene) + ".schema.yaml"),
          std::string("schema:\n  schema_id: ") + scene + "\n  name: " + scene +
          "\n  version: '1.0'\nengine:\n  processors: [speller, selector, navigator, express_editor]\n"
          "  segmentors: [abc_segmentor, fallback_segmentor]\n  translators: [script_translator]\n"
          "speller:\n  alphabet: abcdefghijklmnopqrstuvwxyz\n  delimiter: \" '\"\n"
          "translator:\n  dictionary: base\n  user_dict: " + scene +
          "\n  prism: " + scene + "\n  enable_user_dict: true\n  enable_sentence: true\n");
  }
}
static void Open(const fs::path& root) {
  static std::string directory;
  directory = root.u8string();
  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = traits.user_data_dir = directory.c_str();
  traits.distribution_name = "Isolated learning test";
  traits.distribution_code_name = "shorekeeper-test";
  traits.distribution_version = "1.0";
  traits.app_name = "rime.shorekeeper_test";
  traits.log_dir = directory.c_str();
  api->initialize(&traits);
  api->start_maintenance(True); api->join_maintenance_thread();
}
static void Learn(const char* schema, const char* input, const char* expected) {
  auto session = api->create_session(); Require(session != 0, "no session");
  Require(api->select_schema(session, schema), "schema selection failed");
  Require(api->simulate_key_sequence(session, input), "input rejected");
  RIME_STRUCT(RimeCommit, commit);
  Require(api->get_commit(session, &commit), "no committed sentence");
  bool matches = commit.text && std::string(commit.text) == expected;
  api->free_commit(&commit); api->destroy_session(session);
  Require(matches, "unexpected committed sentence");
}
static void Sync() {
  api->cleanup_all_sessions(); Require(api->sync_user_data(), "sync failed");
  api->join_maintenance_thread();
}
static std::string Snapshot(const fs::path& root, const char* id, const char* scene) {
  std::ifstream in(root / "sync" / id / (std::string(scene) + ".userdb.txt"), std::ios::binary);
  Require((bool)in, "snapshot missing");
  return {std::istreambuf_iterator<char>(in), {}};
}
int main(int argc, char** argv) {
  try {
    Require(argc == 2, "provide isolated test root");
    fs::path root = fs::absolute(fs::u8path(argv[1]));
    Require(!fs::exists(root), "test root must not exist");
    auto windows = root / "windows", phone = root / "phone";
    Fixture(windows, "windows-test"); Fixture(phone, "phone-test");
    api = rime_get_api(); RIME_STRUCT(RimeTraits, setup);
    setup.app_name = "rime.shorekeeper_test"; setup.min_log_level = 2; api->setup(&setup);
    Open(windows);
    Learn("work", "nihao{space}", u8"你好");
    Learn("daily", "haoni{space}", u8"好你"); Sync(); api->finalize();
    Require(Snapshot(windows, "windows-test", "work").find(u8"你好\t") != std::string::npos, "work phrase was not learned");
    Require(Snapshot(windows, "windows-test", "daily").find(u8"你好\t") == std::string::npos, "work learning leaked into daily");
    fs::create_directories(phone / "sync");
    fs::copy(windows / "sync" / "windows-test", phone / "sync" / "windows-test", fs::copy_options::recursive);
    Open(phone); Sync(); Learn("work", "nihaoni{space}", u8"你好你"); Sync(); api->finalize();
    fs::copy(phone / "sync" / "phone-test", windows / "sync" / "phone-test", fs::copy_options::recursive);
    Open(windows); Sync();
    Require(Snapshot(windows, "windows-test", "work").find(u8"你好你\t") != std::string::npos, "remote phrase not merged");
    Sync(); api->finalize();
    Require(Snapshot(windows, "windows-test", "daily").find(u8"你好你\t") == std::string::npos, "remote work learning leaked into daily");
    std::cout << "PASS official Rime learning isolation, phrase snapshots and two-device repeated merge\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
