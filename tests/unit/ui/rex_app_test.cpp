#include <rex/rex_app.h>

#include <catch2/catch_test_macros.hpp>

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
};

}  // namespace

TEST_CASE("ReXApp accepts game data root as the first positional argument", "[ui][rex_app]") {
  TestWindowedAppContext context;
  TestReXApp app(context);

  const auto& options = app.GetPositionalOptions();

  REQUIRE(options.size() == 1);
  CHECK(options[0] == "game_data_root");
}
