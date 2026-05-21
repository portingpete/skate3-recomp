#include <rex/ui/imgui_dialog.h>

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

TEST_CASE("ImGuiDialog can be destroyed through its base type", "[ui][imgui_dialog]") {
  STATIC_REQUIRE(std::has_virtual_destructor_v<rex::ui::ImGuiDialog>);
}