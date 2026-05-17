/**
 * @file        tests/unit/rexglue/codegen_command_test.cpp
 * @brief       Tests for codegen command migration application behavior
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license     BSD 3-Clause License
 */

#include "rexglue/commands/codegen_command.cpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct TempDir {
  fs::path path;

  TempDir() : path(fs::temp_directory_path() / "codegen_command_test") {
    fs::remove_all(path);
    fs::create_directories(path);
  }

  ~TempDir() { fs::remove_all(path); }
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

TEST_CASE("CodegenCommand: migration plan writes files directly under project root",
          "[rexglue][codegen_command]") {
  TempDir tmp;
  auto old_cwd = fs::current_path();
  fs::current_path(tmp.path);
  auto restore_cwd = [&] { fs::current_path(old_cwd); };

  std::vector<rexglue::cli::OverwriteEntry> plan = {{
      fs::path("doritos_crash_course_manifest.toml"),
      "[project]\nname = \"doritos_crash_course\"\n",
      rexglue::cli::OverwriteAction::Write,
      false,
      "upgrade legacy config",
  }};

  auto result = rexglue::cli::ApplyPlan(plan);
  restore_cwd();

  REQUIRE(result);
  CHECK(ReadFile(tmp.path / "doritos_crash_course_manifest.toml")
            .find("doritos_crash_course") != std::string::npos);
}

TEST_CASE("CodegenCommand: root-relative migration scans use current project directory",
          "[rexglue][codegen_command]") {
  TempDir tmp;
  WriteFile(tmp.path / "src" / "main.cpp",
            "#include \"generated/doritos_crash_course_config.h\"\n"
            "#include \"generated/doritos_crash_course_init.h\"\n");

  auto old_cwd = fs::current_path();
  fs::current_path(tmp.path);
  auto findings =
      rexglue::cli::ScanProjectMigrations(fs::path{}, "doritos_crash_course", "0.8.0",
                                          "generated");
  fs::current_path(old_cwd);

  auto rewritten = std::find_if(
      findings.rewrites.begin(), findings.rewrites.end(), [](const rexglue::cli::OverwriteEntry& e) {
        return e.path.filename() == "main.cpp" &&
               e.rendered_content.find("doritos_crash_course_config.h") == std::string::npos &&
               e.rendered_content.find("doritos_crash_course_init.h") != std::string::npos;
      });
  REQUIRE(rewritten != findings.rewrites.end());
}

TEST_CASE("CodegenCommand: migration rewrites compose when one source needs multiple upgrades",
          "[rexglue][codegen_command]") {
  TempDir tmp;
  WriteFile(tmp.path / "src" / "hooks.cpp",
            "#include \"generated/doritos_crash_course_config.h\"\n"
            "#include <rex/ppc/memory.h>\n"
            "bool DccPreparePhysicalWriteAccess(PPCContext& ctx, uint32_t guest_addr, "
            "uint32_t byte_count) {\n"
            "  if (!ctx.kernel_state || !ctx.kernel_state->memory()) {\n"
            "    return false;\n"
            "  }\n"
            "  return ctx.kernel_state->memory()->PreparePhysicalWriteAccess(guest_addr, "
            "byte_count);\n"
            "}\n"
            "uint32_t Load(uint32_t addr) { return PPC_LOAD_U32(addr); }\n");

  auto findings =
      rexglue::cli::ScanProjectMigrations(tmp.path, "doritos_crash_course", "0.8.0",
                                          "generated");
  REQUIRE(rexglue::cli::ApplyPlan(findings.rewrites));

  std::string rewritten = ReadFile(tmp.path / "src" / "hooks.cpp");
  CHECK(rewritten.find("doritos_crash_course_config.h") == std::string::npos);
  CHECK(rewritten.find("rex/ppc/memory.h") == std::string::npos);
  CHECK(rewritten.find("PPC_LOAD_U32") == std::string::npos);
  CHECK(rewritten.find("ctx.kernel_state") == std::string::npos);
  CHECK(rewritten.find("->PreparePhysicalWriteAccess") == std::string::npos);
  CHECK(rewritten.find("doritos_crash_course_init.h") != std::string::npos);
  CHECK(rewritten.find("#include <rex/memory.h>") != std::string::npos);
  CHECK(rewritten.find("REX_LOAD_U32(addr)") != std::string::npos);
  CHECK(rewritten.find("TriggerPhysicalMemoryCallbacks(") != std::string::npos);
}
