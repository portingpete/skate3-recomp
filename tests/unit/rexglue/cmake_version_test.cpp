/**
 * @file        tests/unit/rexglue/cmake_version_test.cpp
 * @brief       Tests for RexGlue CMake version provenance plumbing
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license     BSD 3-Clause License
 */

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#ifndef REXGLUE_TEST_SOURCE_DIR
#error "REXGLUE_TEST_SOURCE_DIR must point at the RexGlue source tree"
#endif

namespace fs = std::filesystem;

namespace {

std::string ReadTextFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("RexGlue CMake version resolves from SDK source when embedded",
          "[rexglue][cmake][version]") {
  const auto source_root = fs::path(REXGLUE_TEST_SOURCE_DIR);
  const std::string cmake_lists = ReadTextFile(source_root / "CMakeLists.txt");

  const auto version_call = cmake_lists.find("rex_resolve_version(REXGLUE_FULL_VERSION");
  REQUIRE(version_call != std::string::npos);
  const auto header_config = cmake_lists.find("configure_file(", version_call);
  REQUIRE(header_config != std::string::npos);

  const std::string version_block =
      cmake_lists.substr(version_call, header_config - version_call);
  CHECK(version_block.find("SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}") !=
        std::string::npos);
}
