#include <rex/rex_app.h>
#include <rex/version.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

namespace {

class TestWindowedAppContext : public rex::ui::WindowedAppContext {
 protected:
  void NotifyUILoopOfPendingFunctions() override {}
  void PlatformQuitFromUIThread() override {}
};

class TestReXApp final : public rex::ReXApp {
 public:
  explicit TestReXApp(rex::ui::WindowedAppContext& context)
      : rex::ReXApp(context, "test_app", rex::PPCImageInfo{}) {}

  static std::filesystem::path ResolveDefaultCacheRootForTest(
      std::string_view app_name, const std::filesystem::path& user_data_root,
      const std::filesystem::path& local_cache_root, std::string_view cache_root) {
    return ResolveDefaultCacheRoot(app_name, user_data_root, local_cache_root, cache_root);
  }

  static std::filesystem::path ResolveDefaultUpdateDataRootForTest(
      const std::filesystem::path& game_root, std::string_view update_data_root) {
    return ResolveDefaultUpdateDataRoot(game_root, update_data_root);
  }

  static std::string BuildGameDataRootMissingMessageForTest(std::string_view app_name) {
    return BuildGameDataRootMissingMessage(app_name);
  }

  static std::string BuildGameDataRootMissingMessageForTest(std::string_view app_name,
                                                            std::string_view parse_error) {
    return BuildGameDataRootMissingMessage(app_name, parse_error);
  }

  static std::string BuildHostBuildLineForTest() {
    return BuildHostBuildLine();
  }

  static std::string BuildGeneratedBuildLineForTest(std::string_view generated_build_stamp) {
    return BuildGeneratedBuildLine(generated_build_stamp);
  }

  static std::string BuildGeneratedEntrypointXexHashLineForTest(std::string_view sha256) {
    return BuildGeneratedEntrypointXexHashLine(sha256);
  }

  static std::string BuildExecutablePathLineForTest(
      const std::filesystem::path& executable_path) {
    return BuildExecutablePathLine(executable_path);
  }

  static std::string BuildLoadedXexExecutionInfoLineForTest(uint32_t title_id,
                                                            uint32_t media_id,
                                                            uint32_t version_value,
                                                            uint8_t disc_number,
                                                            uint8_t disc_count) {
    return BuildLoadedXexExecutionInfoLine(title_id, media_id, version_value, disc_number,
                                           disc_count);
  }

  static std::string BuildLoadedXexSystemFlagsLineForTest(uint32_t system_flags) {
    return BuildLoadedXexSystemFlagsLine(system_flags);
  }

  static std::string BuildGeneratedEntrypointMismatchLineForTest(
      std::string_view generated_sha256, std::string_view loaded_sha256,
      const std::filesystem::path& loaded_path) {
    return BuildGeneratedEntrypointMismatchLine(generated_sha256, loaded_sha256, loaded_path);
  }

  static std::string BuildGameDataRootNotFoundMessageForTest(
      const std::filesystem::path& game_data_root) {
    return BuildGameDataRootNotFoundMessage(game_data_root);
  }
};

std::filesystem::path MakeTempGameRoot(std::string_view name) {
  auto root = std::filesystem::temp_directory_path() /
              ("rex_app_" + std::string(name) + "_" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root);
  return root;
}

}  // namespace

TEST_CASE("ReXApp accepts game data root as the first positional argument", "[ui][rex_app]") {
  TestWindowedAppContext context;
  TestReXApp app(context);

  const auto& options = app.GetPositionalOptions();

  REQUIRE(options.size() == 1);
  CHECK(options[0] == "game_data_root");
}

TEST_CASE("ReXApp uses explicit update data root before game defaults",
          "[ui][rex_app][paths]") {
  auto root = MakeTempGameRoot("explicit_update");
  std::filesystem::create_directories(root / "update");
  auto explicit_update = root.parent_path() / (root.filename().string() + "_override");
  std::filesystem::create_directories(explicit_update);

  CHECK(TestReXApp::ResolveDefaultUpdateDataRootForTest(root, explicit_update.string()) ==
        explicit_update);

  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::remove_all(explicit_update, ec);
}

TEST_CASE("ReXApp uses game update directory as default update data root",
          "[ui][rex_app][paths]") {
  auto root = MakeTempGameRoot("with_update");
  std::filesystem::create_directories(root / "update" / "data");

  CHECK(TestReXApp::ResolveDefaultUpdateDataRootForTest(root, "") == root / "update");

  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

TEST_CASE("ReXApp leaves update data root empty when game has no update directory",
          "[ui][rex_app][paths]") {
  auto root = MakeTempGameRoot("without_update");

  CHECK(TestReXApp::ResolveDefaultUpdateDataRootForTest(root, "").empty());

  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

TEST_CASE("ReXApp defaults shader cache outside the user documents root",
          "[ui][rex_app][paths]") {
  auto user_root = std::filesystem::path("C:/Users/test/OneDrive/Documents/test_app");
  auto local_cache_root = std::filesystem::path("C:/Users/test/AppData/Local");

  CHECK(TestReXApp::ResolveDefaultCacheRootForTest("test_app", user_root, local_cache_root, "") ==
        local_cache_root / "RexGlue" / "test_app" / "cache");
}

TEST_CASE("ReXApp keeps explicit shader cache root overrides", "[ui][rex_app][paths]") {
  auto user_root = std::filesystem::path("C:/Users/test/OneDrive/Documents/test_app");
  auto local_cache_root = std::filesystem::path("C:/Users/test/AppData/Local");
  auto explicit_cache_root = std::filesystem::path("C:/tmp/rex-cache");

  CHECK(TestReXApp::ResolveDefaultCacheRootForTest("test_app", user_root, local_cache_root,
                                                   explicit_cache_root.string()) ==
        explicit_cache_root);
}

TEST_CASE("ReXApp falls back to user data cache when local cache root is unavailable",
          "[ui][rex_app][paths]") {
  auto user_root = std::filesystem::path("C:/Users/test/Documents/test_app");

  CHECK(TestReXApp::ResolveDefaultCacheRootForTest("test_app", user_root, {}, "") ==
        user_root / "cache");
}

TEST_CASE("ReXApp game data root errors explain expected launch argument",
          "[ui][rex_app][paths]") {
  auto missing = TestReXApp::BuildGameDataRootMissingMessageForTest("test_app");
  CHECK(missing.find("Game data root was not provided for test_app.") != std::string::npos);
  CHECK(missing.find("--game-data-root <folder>") != std::string::npos);
  CHECK(missing.find("final positional argument") != std::string::npos);

  auto missing_after_parse_error =
      TestReXApp::BuildGameDataRootMissingMessageForTest("test_app",
                                                         "--game-data-root: missing value");
  CHECK(missing_after_parse_error.find("Game data root was not provided for test_app.") !=
        std::string::npos);
  CHECK(missing_after_parse_error.find("One launch option could not be parsed") !=
        std::string::npos);
  CHECK(missing_after_parse_error.find("--game-data-root: missing value") != std::string::npos);

  auto option_value =
      TestReXApp::BuildGameDataRootNotFoundMessageForTest(std::filesystem::path("true"));
  CHECK(option_value.find("Game data root does not exist: true") != std::string::npos);
  CHECK(option_value.find("--game-data-root <folder>") != std::string::npos);
  CHECK(option_value.find("looks like an option value") != std::string::npos);

  auto option_name =
      TestReXApp::BuildGameDataRootNotFoundMessageForTest(std::filesystem::path("--log_level"));
  CHECK(option_name.find("Game data root does not exist: --log_level") != std::string::npos);
  CHECK(option_name.find("looks like an option value") != std::string::npos);

  auto user_root_alias =
      TestReXApp::BuildGameDataRootNotFoundMessageForTest(std::filesystem::path("--user-root"));
  CHECK(user_root_alias.find("Game data root does not exist: --user-root") != std::string::npos);
  CHECK(user_root_alias.find("Did you mean --user-data-root?") != std::string::npos);
}

TEST_CASE("ReXApp startup identity lines include host build and executable path",
          "[ui][rex_app][paths]") {
  auto build_line = TestReXApp::BuildHostBuildLineForTest();
  CHECK(build_line.find("Host build:") != std::string::npos);
  CHECK(build_line.find(REXGLUE_BUILD_STAMP) != std::string::npos);

  auto generated_line =
      TestReXApp::BuildGeneratedBuildLineForTest("build: rexglue-v0.8.0-old-win-amd64-Debug");
  CHECK(generated_line.find("Generated code build:") != std::string::npos);
  CHECK(generated_line.find("rexglue-v0.8.0-old") != std::string::npos);

  auto missing_generated_line = TestReXApp::BuildGeneratedBuildLineForTest("");
  CHECK(missing_generated_line.find("Generated code build: unavailable") != std::string::npos);
  CHECK(missing_generated_line.find("regenerate") != std::string::npos);

  auto generated_xex_hash_line = TestReXApp::BuildGeneratedEntrypointXexHashLineForTest(
      "e55b6d34654a3f1ab9eb6c4d88203e82ee0c96cc425ed5a643573a3875e942e0");
  CHECK(generated_xex_hash_line.find("Generated entrypoint XEX SHA256:") != std::string::npos);
  CHECK(generated_xex_hash_line.find(
            "e55b6d34654a3f1ab9eb6c4d88203e82ee0c96cc425ed5a643573a3875e942e0") !=
        std::string::npos);

  auto missing_xex_hash_line = TestReXApp::BuildGeneratedEntrypointXexHashLineForTest("");
  CHECK(missing_xex_hash_line.find("Generated entrypoint XEX SHA256: unavailable") !=
        std::string::npos);
  CHECK(missing_xex_hash_line.find("regenerate") != std::string::npos);

  auto executable = std::filesystem::path("C:/tmp/rex-hosts/gtaiv_disc1/gtaiv_disc1.exe");
  auto executable_line = TestReXApp::BuildExecutablePathLineForTest(executable);
  CHECK(executable_line.find("Executable:") != std::string::npos);
  CHECK(executable_line.find(executable.string()) != std::string::npos);
}

TEST_CASE("ReXApp startup identity lines include loaded XEX media details",
          "[ui][rex_app][paths]") {
  constexpr uint32_t kVersion = (1u << 28) | (2u << 24) | (345u << 8) | 6u;

  auto execution_line = TestReXApp::BuildLoadedXexExecutionInfoLineForTest(
      0x545407F2, 0x06759F9C, kVersion, 1, 2);
  CHECK(execution_line.find("Loaded XEX:") != std::string::npos);
  CHECK(execution_line.find("title_id=545407F2") != std::string::npos);
  CHECK(execution_line.find("media_id=06759F9C") != std::string::npos);
  CHECK(execution_line.find("version=1.2.345.6") != std::string::npos);
  CHECK(execution_line.find("disc=1/2") != std::string::npos);

  auto no_multidisc_flags = TestReXApp::BuildLoadedXexSystemFlagsLineForTest(0x00000200);
  CHECK(no_multidisc_flags.find("Loaded XEX system flags: 00000200") != std::string::npos);
  CHECK(no_multidisc_flags.find("multidisc=none") != std::string::npos);

  auto multidisc_flags = TestReXApp::BuildLoadedXexSystemFlagsLineForTest(0x08018000);
  CHECK(multidisc_flags.find("multidisc_swap") != std::string::npos);
  CHECK(multidisc_flags.find("multidisc_insecure_media") != std::string::npos);
  CHECK(multidisc_flags.find("multidisc_cross_title") != std::string::npos);
}

TEST_CASE("ReXApp warns when loaded entrypoint hash differs from generated XEX",
          "[ui][rex_app][paths]") {
  auto line = TestReXApp::BuildGeneratedEntrypointMismatchLineForTest(
      "e55b6d34654a3f1ab9eb6c4d88203e82ee0c96cc425ed5a643573a3875e942e0",
      "fa76a67b0be6138783bbd7dfc7d231adbb536f4ae188965085485aae1670a6af",
      "K:/DoritosRexGlue/games/GTAIV/disc2/default.xex");
  CHECK(line.find("Generated entrypoint XEX SHA256 does not match loaded XEX") !=
        std::string::npos);
  CHECK(line.find("disc2/default.xex") != std::string::npos);
}
