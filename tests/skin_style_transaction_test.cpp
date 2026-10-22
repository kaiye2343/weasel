// Exercises the service transaction seam with the actual resource publisher.
#include <windows.h>
#include <gdiplus.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include "../include/WeaselSkinAssets.h"
#include "../include/WeaselSkinTransaction.h"
#pragma comment(lib, "gdiplus.lib")

namespace fs = std::filesystem;
struct Style {
  std::vector<std::wstring> paths;
  int layout = 0;
  bool operator==(const Style& other) const {
    return paths == other.paths && layout == other.layout;
  }
};
static void Require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
int wmain(int argc, wchar_t** argv) {
  Gdiplus::GdiplusStartupInput input;
  ULONG_PTR gdip = 0;
  if (Gdiplus::GdiplusStartup(&gdip, &input, nullptr) != Gdiplus::Ok) return 1;
  int result = 0;
  try {
    Require(argc == 3, "arguments: PNG isolated-root");
    const fs::path source = fs::absolute(argv[1]);
    const fs::path root = fs::absolute(argv[2]);
    fs::create_directories(root);
    weasel::SkinAssetPublisher publisher(root / L"cache", root / L"private-state");
    std::string error;
    auto publish = [&](int, const Style& old, Style& next, std::string& reason) {
      std::vector<std::wstring> paths;
      if (!publisher.Publish(old.paths, paths, reason)) return false;
      next = old;
      next.paths = std::move(paths);
      return true;
    };
    const std::map<int, Style> current = {
        {1, {{source.wstring()}, 11}},
        {2, {{(root / L"missing.png").wstring()}, 22}}};
    const auto currentBefore = current;
    std::map<int, Style> proposed = {{99, {{L"sentinel"}, 99}}};
    const auto sentinel = proposed;
    Require(!weasel::PrepareSkinStyleTransaction(current, proposed, publish, error),
            "second-session failure must fail the transaction");
    Require(current == currentBefore && proposed == sentinel,
            "failed transaction changed current or proposed styles");

    std::map<int, Style> valid = {{1, {{source.wstring()}, 11}},
                                 {2, {{source.wstring()}, 22}}};
    const auto validBefore = valid;
    Require(weasel::PrepareSkinStyleTransaction(valid, proposed, publish, error),
            "multi-session publication failed");
    Require(valid == validBefore && proposed.size() == 2 &&
                proposed.at(1).layout == 11 && proposed.at(2).layout == 22 &&
                proposed.at(1).paths == proposed.at(2).paths &&
                fs::path(proposed.at(1).paths.at(0)).parent_path() == publisher.Root(),
            "successful transaction lost geometry or published digest paths");
    const auto publishedBefore = proposed;
    auto failReload = [&](int key, const Style& old, Style& next, std::string& reason) {
      if (key == 2) { reason = "injected second-session failure"; return false; }
      return publish(key, old, next, reason);
    };
    std::map<int, Style> reload = proposed;
    Require(!weasel::PrepareSkinStyleTransaction(proposed, reload, failReload, error) &&
                proposed == publishedBefore && reload == publishedBefore,
            "failed refresh replaced previously successful styles");
    const std::map<int, Style> empty;
    Require(weasel::PrepareSkinStyleTransaction(empty, reload, publish, error) && reload.empty(),
            "empty session transaction failed");

    const auto plainRoot = root / L"pure-color";
    weasel::SkinAssetPublisher plainPublisher(plainRoot / L"cache", plainRoot / L"state");
    const std::map<int, Style> plain = {{1, {{}, 33}}};
    auto publishPlain = [&](int, const Style& old, Style& next, std::string& reason) {
      next = old;
      return plainPublisher.Publish(old.paths, next.paths, reason);
    };
    Require(weasel::PrepareSkinStyleTransaction(plain, reload, publishPlain, error) &&
                reload == plain && !fs::exists(plainRoot),
            "pure-color transaction unnecessarily created resource cache");
    std::cout << "PASS multi-session preparation, atomic failure, previous-style rollback, digest reuse, empty sessions and pure colors\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    result = 1;
  }
  Gdiplus::GdiplusShutdown(gdip);
  return result;
}
