/**
 * @file        tests/unit/rexglue/init_command_test.cpp
 * @brief       Tests for rexglue init project path handling
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license     BSD 3-Clause License
 */

#include "rexglue/commands/init_command.cpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

struct TempDir {
  fs::path path;

  TempDir() : path(fs::temp_directory_path() / "init_command_test") {
    fs::remove_all(path);
    fs::create_directories(path);
  }

  ~TempDir() { fs::remove_all(path); }
};

struct CwdGuard {
  fs::path old;

  explicit CwdGuard(const fs::path& next) : old(fs::current_path()) { fs::current_path(next); }
  ~CwdGuard() { fs::current_path(old); }
};

struct UiGuard {
  std::ostringstream out;

  UiGuard() {
    rexglue::ui::Shutdown();
    rexglue::ui::detail::SetGlobalSinkForTesting(
        std::make_unique<rexglue::ui::PresentationSink>(out, /*tty=*/false,
                                                        /*color=*/false));
  }

  ~UiGuard() { rexglue::ui::Shutdown(); }
};

std::string ReadFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void WriteFile(const fs::path& path, const std::string& content) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out << content;
}

}  // namespace

TEST_CASE("InitProject resolves relative game paths against explicit project root",
          "[rexglue][init_command]") {
  TempDir tmp;
  fs::path cwd = tmp.path / "launcher";
  fs::path project = tmp.path / "project";
  fs::create_directories(cwd);
  WriteFile(project / "assets" / "default.xex", "dummy xex");
  CwdGuard cwd_guard(cwd);
  UiGuard ui_guard;

  rexglue::cli::InitOptions opts;
  opts.project_name = "Castle Crashers";
  opts.project_root = project.string();
  opts.xex_path = "assets/default.xex";
  opts.game_root = "assets";

  rexglue::cli::CliContext ctx;
  auto result = rexglue::cli::InitProject(opts, ctx);

  REQUIRE(result);
  std::string manifest = ReadFile(project / "castle_crashers_manifest.toml");
  CHECK(manifest.find("file_path = \"assets/default.xex\"") != std::string::npos);
  CHECK(manifest.find("game_root = \"assets\"") != std::string::npos);
}
