/**
 * @file        tests/unit/codegen/template_registry_test.cpp
 * @brief       Unit tests for TemplateRegistry
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license     BSD 3-Clause License
 */

#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/template_registry.h>

#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;

// Helper to create a temp directory for test overrides
static fs::path CreateTempDir() {
  auto tmp = fs::temp_directory_path() / "rex_template_test";
  fs::create_directories(tmp);
  return tmp;
}

static void CleanupTempDir(const fs::path& dir) {
  fs::remove_all(dir);
}

static void WriteTempFile(const fs::path& path, const std::string& content) {
  fs::create_directories(path.parent_path());
  std::ofstream f(path);
  f << content;
}

static size_t CountOccurrences(const std::string& text, const std::string& needle) {
  size_t count = 0;
  size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

TEST_CASE("TemplateRegistry: registeredIds returns all template IDs", "[TemplateRegistry]") {
  rex::codegen::TemplateRegistry registry;
  auto ids = registry.registeredIds();

  REQUIRE(ids.size() == 17);

  auto has = [&](const std::string& id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
  };
  CHECK(has("init/cmakelists"));
  CHECK(has("init/cmake_presets"));
  CHECK(has("init/main_cpp"));
  CHECK(has("init/app_header"));
  CHECK(has("init/manifest_toml"));
  CHECK(has("init/rexglue_cmake"));
  CHECK(has("codegen/init_h"));
  CHECK(has("codegen/init_cpp"));
  CHECK(has("codegen/sources_cmake"));
  CHECK(has("codegen/_indirect_call"));
  CHECK(has("codegen/dll_targets_cmake"));
  CHECK(has("codegen/module_registry_cpp"));
  CHECK(has("codegen/register_cpp"));
  CHECK(has("test/ppc_config_h"));
  CHECK(has("test/ppc_test_cases_cpp"));
  CHECK(has("test/ppc_test_decls_h"));
  CHECK(has("test/ppc_test_functions_cpp"));
}

TEST_CASE("TemplateRegistry: render with simple CLI data", "[TemplateRegistry]") {
  rex::codegen::TemplateRegistry registry;
  std::string json = R"({
    "names": {"snake_case": "test_app"},
    "sdk_version": "1.0.0",
    "sdk_version_full": "1.0.0-test",
    "generated_on": "2026-05-16T00:00:00Z",
    "include_stamp": false,
    "game_root": "",
    "xex_path": "assets/default.xex",
    "out_directory_path": "generated/default",
    "modules": []
  })";
  std::string result = registry.render("init/manifest_toml", json);

  CHECK(result.find("name = \"test_app\"") != std::string::npos);
  CHECK(result.find("[entrypoint]") != std::string::npos);
  CHECK(result.find("file_path = \"assets/default.xex\"") != std::string::npos);
}

TEST_CASE("Template: manifest_toml enables generated exception handlers by default",
          "[TemplateRegistry]") {
  rex::codegen::TemplateRegistry registry;
  std::string json = R"({
    "names": {"snake_case": "test_app"},
    "sdk_version": "1.0.0",
    "sdk_version_full": "1.0.0-test",
    "generated_on": "2026-05-16T00:00:00Z",
    "include_stamp": false,
    "game_root": "",
    "xex_path": "assets/default.xex",
    "out_directory_path": "generated/default",
    "modules": []
  })";
  std::string result = registry.render("init/manifest_toml", json);

  CHECK(result.find("generate_exception_handlers = true") != std::string::npos);
}

TEST_CASE("TemplateRegistry: render with codegen data", "[TemplateRegistry]") {
  rex::codegen::TemplateRegistry registry;
  std::string json = R"({
    "project": "test_proj",
    "image_base": "0x82000000",
    "image_size": "0x1000000",
    "code_base": "0x82010000",
    "code_size": "0x100000",
    "rexcrt_heap": 1,
    "has_dll_modules": false,
    "is_dll": false,
    "config_flags": {},
    "functions": [],
    "imports": []
  })";

  std::string result = registry.render("codegen/init_cpp", json);
  CHECK(result.find("PPCImageConfig") != std::string::npos);
  CHECK(result.find("test_proj") != std::string::npos);
}

TEST_CASE("Template: init cmake presets enable AMD64 baseline SIMD", "[TemplateRegistry]") {
  rex::codegen::TemplateRegistry registry;
  std::string result = registry.render("init/cmake_presets", "{}");

  CHECK(result.find("\"CMAKE_C_FLAGS\": \"-march=x86-64-v3\"") != std::string::npos);
  CHECK(result.find("\"CMAKE_CXX_FLAGS\": \"-march=x86-64-v3\"") != std::string::npos);
  CHECK(result.find("\"CMAKE_C_FLAGS\": \"-march=armv8-a\"") != std::string::npos);
  CHECK(result.find("\"CMAKE_CXX_FLAGS\": \"-march=armv8-a\"") != std::string::npos);
}

TEST_CASE("Template: rexglue target setup exposes source-tree ImGui includes",
          "[TemplateRegistry]") {
  rex::codegen::TemplateRegistry registry;
  std::string json =
      R"({"sdk_version": "0.8.0", "entrypoint_out_dir": "generated", "names": {"snake_case": "mygame"}})";
  std::string result = registry.render("init/rexglue_cmake", json);

  CHECK(result.find("if(TARGET imgui)") != std::string::npos);
  CHECK(result.find("$<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>") !=
        std::string::npos);
}

TEST_CASE("Template: rexglue target setup applies generated SEH compile options",
          "[TemplateRegistry]") {
  rex::codegen::TemplateRegistry registry;
  std::string json =
      R"({"sdk_version": "0.8.0", "entrypoint_out_dir": "generated/default", "names": {"snake_case": "mygame"}})";
  std::string result = registry.render("init/rexglue_cmake", json);

  CHECK(result.find("GENERATED_USES_SEH") != std::string::npos);
  CHECK(result.find("CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL \"MSVC\"") !=
        std::string::npos);
  CHECK(result.find("target_compile_options(${target_name} PRIVATE /EHa)") !=
        std::string::npos);
  CHECK(result.find("target_compile_options(${target_name} PRIVATE -fexceptions)") !=
        std::string::npos);
}

TEST_CASE("Template: sources_cmake records generated SEH usage", "[TemplateRegistry]") {
  rex::codegen::TemplateRegistry registry;
  std::string json = R"({
    "project": "test_proj",
    "generate_exception_handlers": true,
    "has_dll_modules": false,
    "is_dll": false,
    "recomp_files": ["test_proj_recomp.0.cpp"]
  })";
  std::string result = registry.render("codegen/sources_cmake", json);

  CHECK(result.find("set(GENERATED_USES_SEH ON)") != std::string::npos);
}

TEST_CASE("TemplateRegistry: render unknown ID throws TemplateError", "[TemplateRegistry]") {
  rex::codegen::TemplateRegistry registry;
  REQUIRE_THROWS_AS(registry.render("nonexistent/template_id", "{}"), rex::codegen::TemplateError);
}

TEST_CASE("TemplateRegistry: init_h includes shared indirect-call partial", "[TemplateRegistry]") {
  rex::codegen::TemplateRegistry registry;
  std::string json = R"({
    "config_flags": {
      "skip_lr": false,
      "ctr_as_local": false,
      "xer_as_local": false,
      "reserved_as_local": false,
      "skip_msr": false,
      "cr_as_local": false,
      "non_argument_as_local": false,
      "non_volatile_as_local": false
    },
    "image_base": "0x82000000",
    "image_size": "0x1000000",
    "code_base": "0x82010000",
    "code_size": "0x100000",
    "thunk_reserve_size": "0x1000",
    "rexcrt_heap": false,
    "functions": [],
    "imports": []
  })";
  std::string result = registry.render("codegen/init_h", json);
  CHECK(result.find("REX_LOOKUP_FUNC") != std::string::npos);
  CHECK(result.find("ResolveIndirectFunction") != std::string::npos);
  CHECK(result.find("last_indirect_target") != std::string::npos);
  CHECK(result.find("REX_CALL_INDIRECT_FUNC_AT") != std::string::npos);
  CHECK(result.find("REX_THUNK_RESERVE_SIZE") != std::string::npos);
  CHECK(result.find("[[likely]]") != std::string::npos);
  CHECK(result.find("[[unlikely]]") != std::string::npos);
  CHECK(result.find("REX_CALL_NATIVE_FUNC") != std::string::npos);
}

TEST_CASE("TemplateRegistry: init_h includes SEH support for generated handlers",
          "[TemplateRegistry]") {
  rex::codegen::TemplateRegistry registry;
  std::string json = R"({
    "config_flags": {
      "skip_lr": false,
      "ctr_as_local": false,
      "xer_as_local": false,
      "reserved_as_local": false,
      "skip_msr": false,
      "cr_as_local": false,
      "non_argument_as_local": false,
      "non_volatile_as_local": false
    },
    "image_base": "0x82000000",
    "image_size": "0x1000000",
    "code_base": "0x82010000",
    "code_size": "0x100000",
    "thunk_reserve_size": "0x1000",
    "rexcrt_heap": false,
    "functions": [],
    "imports": []
  })";
  std::string result = registry.render("codegen/init_h", json);
  CHECK(result.find("#include <rex/platform/exceptions.h>") != std::string::npos);
}

TEST_CASE("TemplateRegistry: ppc_config_h includes shared indirect-call partial",
          "[TemplateRegistry]") {
  rex::codegen::TemplateRegistry registry;
  std::string json = R"({
    "image_base": "0x82000000",
    "image_size": "0x1000000",
    "code_base": "0x82010000",
    "code_size": "0x100000",
    "thunk_reserve_size": "0x1000"
  })";
  std::string result = registry.render("test/ppc_config_h", json);
  CHECK(result.find("ResolveIndirectFunction") != std::string::npos);
  CHECK(result.find("last_indirect_target") != std::string::npos);
  CHECK(result.find("REX_CALL_INDIRECT_FUNC_AT") != std::string::npos);
  CHECK(result.find("[[likely]]") != std::string::npos);
  CHECK(result.find("REX_CALL_NATIVE_FUNC") != std::string::npos);
}

TEST_CASE("TemplateRegistry: indirect-call macro keeps legacy wrapper single-evaluation",
          "[TemplateRegistry]") {
  rex::codegen::TemplateRegistry registry;
  std::string json = R"({
    "image_base": "0x82000000",
    "image_size": "0x1000000",
    "code_base": "0x82010000",
    "code_size": "0x100000",
    "thunk_reserve_size": "0x1000"
  })";
  std::string result = registry.render("test/ppc_config_h", json);

  CHECK(result.find("#define REX_CALL_INDIRECT_FUNC(x) "
                    "REX_CALL_INDIRECT_FUNC_AT(0, \"\", 0, (x))") !=
        std::string::npos);

  const std::string at_define = "#define REX_CALL_INDIRECT_FUNC_AT";
  const std::string legacy_define = "#define REX_CALL_INDIRECT_FUNC(x)";
  const size_t at_pos = result.find(at_define);
  REQUIRE(at_pos != std::string::npos);
  const size_t legacy_pos = result.find(legacy_define, at_pos);
  REQUIRE(legacy_pos != std::string::npos);
  const std::string at_macro = result.substr(at_pos, legacy_pos - at_pos);

  CHECK(CountOccurrences(at_macro, "(uint32_t)(x)") == 1);
  CHECK(at_macro.find("REX_LOOKUP_FUNC(base, rex_indirect_target_)") != std::string::npos);
  CHECK(at_macro.find("ResolveIndirectFunction(rex_indirect_target_)") != std::string::npos);
}

TEST_CASE("TemplateRegistry: cmake_var callback works", "[TemplateRegistry]") {
  rex::codegen::TemplateRegistry registry;
  std::string result = registry.renderString("{{ cmake_var(\"FOO\") }}", "{}");
  CHECK(result == "${FOO}");
}

TEST_CASE("TemplateRegistry: loadOverrides with valid override", "[TemplateRegistry]") {
  auto tmpDir = CreateTempDir();

  // Write a custom init/manifest_toml.inja override
  WriteTempFile(tmpDir / "init" / "manifest_toml.inja", "custom_override = true\n");

  rex::codegen::TemplateRegistry registry;
  registry.loadOverrides(tmpDir);

  std::string result = registry.render("init/manifest_toml", "{}");
  CHECK(result.find("custom_override = true") != std::string::npos);
  // Should NOT contain the default template content
  CHECK(result.find("[entrypoint]") == std::string::npos);

  CleanupTempDir(tmpDir);
}

TEST_CASE("TemplateRegistry: loadOverrides ignores unknown IDs", "[TemplateRegistry]") {
  auto tmpDir = CreateTempDir();

  // Write a file with unrecognized canonical ID
  WriteTempFile(tmpDir / "unknown" / "template.inja", "should be ignored\n");

  rex::codegen::TemplateRegistry registry;
  // Should not throw
  REQUIRE_NOTHROW(registry.loadOverrides(tmpDir));

  // Unknown template should still not be renderable
  REQUIRE_THROWS_AS(registry.render("unknown/template", "{}"), rex::codegen::TemplateError);

  CleanupTempDir(tmpDir);
}

TEST_CASE("Template: manifest_toml emits sdk_version when include_stamp is true",
          "[TemplateRegistry][manifest]") {
  rex::codegen::TemplateRegistry registry;
  std::string json =
      R"({"names": {"snake_case": "mygame", "pascal_case": "Mygame", "upper_case": "MYGAME"}, "sdk_version": "0.8.0", "sdk_version_full": "0.8.0-test", "generated_on": "2026-05-16T00:00:00Z", "include_stamp": true, "game_root": "", "xex_path": "default.xex", "out_directory_path": "generated/default", "modules": []})";
  std::string out = registry.render("init/manifest_toml", json);
  CHECK(out.find("sdk_version = \"0.8.0\"") != std::string::npos);
  CHECK(out.find("name = \"mygame\"") != std::string::npos);
}

TEST_CASE("Template: manifest_toml omits sdk_version when include_stamp is false",
          "[TemplateRegistry][manifest]") {
  rex::codegen::TemplateRegistry registry;
  std::string json =
      R"({"names": {"snake_case": "mygame", "pascal_case": "Mygame", "upper_case": "MYGAME"}, "sdk_version": "0.8.0", "sdk_version_full": "0.8.0-test", "generated_on": "2026-05-16T00:00:00Z", "include_stamp": false, "game_root": "", "xex_path": "default.xex", "out_directory_path": "generated/default", "modules": []})";
  std::string out = registry.render("init/manifest_toml", json);
  CHECK(out.find("sdk_version") == std::string::npos);
  CHECK(out.find("name = \"mygame\"") != std::string::npos);
}
