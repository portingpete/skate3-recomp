#include <catch2/catch_test_macros.hpp>

#include <rex/string/utf8.h>

TEST_CASE("utf8 suffix helpers match the final codepoints", "[string][utf8]") {
  CHECK(rex::string::utf8_ends_with("shaders\\GenericVS.xvu", ".xvu"));
  CHECK(rex::string::utf8_ends_with_case("shaders\\GenericVS.xvu", ".XVU"));
  CHECK(rex::string::utf8_ends_with_case("shaders\\SpritePS.xpu", ".xPu"));

  CHECK_FALSE(rex::string::utf8_ends_with_case("shaders\\GenericVS.xvu", ".xpu"));
  CHECK_FALSE(rex::string::utf8_ends_with_case("xvu", ".xvu"));
  CHECK(rex::string::utf8_ends_with_case(".xvu", ".xvu"));
}
