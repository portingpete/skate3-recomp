#include <rex/rex_app.h>

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

  static std::filesystem::path ResolveDefaultUpdateDataRootForTest(
      const std::filesystem::path& game_root, std::string_view update_data_root) {
    return ResolveDefaultUpdateDataRoot(game_root, update_data_root);
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
