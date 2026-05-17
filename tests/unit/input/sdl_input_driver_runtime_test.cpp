#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/logging/sink.h>

namespace rex::input::sdl {
void LoadGamepadMappingsFromConfiguredFile();
}

TEST_CASE("Missing SDL gamecontroller mapping file is debug-only", "[input][sdl]") {
  const auto missing_path =
      std::filesystem::temp_directory_path() /
      ("rex_missing_gamecontrollerdb_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt");
  std::error_code cleanup_error;
  std::filesystem::remove(missing_path, cleanup_error);

  rex::InitLogging(nullptr, spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::core(), spdlog::level::trace);

  const auto old_mappings_file = rex::cvar::GetFlagByName("hid_mappings_file");
  REQUIRE(rex::cvar::SetFlagByName("hid_mappings_file", missing_path.string()));

  auto sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::core(), sink);

  rex::input::sdl::LoadGamepadMappingsFromConfiguredFile();

  std::vector<rex::LogEntry> entries;
  sink->CopyEntries(entries);
  rex::RemoveSink(rex::log::core(), sink);
  REQUIRE(rex::cvar::SetFlagByName("hid_mappings_file", old_mappings_file));
  const auto is_mapping_log = [&](const rex::LogEntry& entry) {
    return entry.text.find("SDL GameControllerDB") != std::string::npos &&
           entry.text.find(missing_path.string()) != std::string::npos;
  };

  const auto warning_count =
      std::count_if(entries.begin(), entries.end(), [&](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn && is_mapping_log(entry);
      });
  const auto debug_count =
      std::count_if(entries.begin(), entries.end(), [&](const rex::LogEntry& entry) {
        return entry.level == spdlog::level::debug && is_mapping_log(entry);
      });

  CHECK(debug_count == 1);
  CHECK(warning_count == 0);
}
