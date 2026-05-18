/**
 * @file        perf_counter_test.cpp
 * @brief       Unit tests for performance counter logging
 *
 * @copyright   Copyright (c) 2026 Tom Clay
 * @license     BSD 3-Clause License
 */

#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include <rex/cvar.h>
#include <rex/perf/counter.h>

namespace {

struct PerfCsvTestScope {
  explicit PerfCsvTestScope(std::filesystem::path path)
      : old_perf_log_csv(rex::cvar::GetFlagByName("perf_log_csv")), csv_path(std::move(path)) {
    rex::perf::FlushCsv();
    std::error_code ec;
    std::filesystem::remove(csv_path, ec);
  }

  ~PerfCsvTestScope() {
    rex::perf::FlushCsv();
    rex::cvar::SetFlagByName("perf_log_csv", old_perf_log_csv);
    std::error_code ec;
    std::filesystem::remove(csv_path, ec);
  }

  std::string old_perf_log_csv;
  std::filesystem::path csv_path;
};

}  // namespace

TEST_CASE("perf_log_csv cvar configures frame CSV output", "[perf][counter]") {
  auto csv_path = std::filesystem::temp_directory_path() / "rex_perf_counter_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));

  rex::perf::ConfigureCsvLogPathFromCvar();
  rex::perf::SetCounter(rex::perf::CounterId::kFrameTimeUs, 16666);
  rex::perf::SetCounter(rex::perf::CounterId::kDrawCalls, 7);
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();
  rex::perf::FlushCsv();

  std::ifstream csv(csv_path);
  REQUIRE(csv.is_open());

  std::string header;
  std::string frame;
  REQUIRE(std::getline(csv, header));
  REQUIRE(std::getline(csv, frame));

  CHECK(header.rfind("frame_time_us,fps,draw_calls", 0) == 0);
  CHECK(frame.rfind("16666,0,7", 0) == 0);
}
