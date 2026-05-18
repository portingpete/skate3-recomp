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
#include <vector>

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

std::vector<std::string> SplitCsvRow(const std::string& row) {
  std::vector<std::string> values;
  size_t start = 0;
  while (start <= row.size()) {
    size_t comma = row.find(',', start);
    if (comma == std::string::npos) {
      values.push_back(row.substr(start));
      break;
    }
    values.push_back(row.substr(start, comma - start));
    start = comma + 1;
  }
  return values;
}

size_t CsvColumnIndex(const std::vector<std::string>& header, const std::string& name) {
  for (size_t i = 0; i < header.size(); ++i) {
    if (header[i] == name) {
      return i;
    }
  }
  return std::string::npos;
}

}  // namespace

TEST_CASE("perf_log_csv cvar writes indexed frame CSV output", "[perf][counter]") {
  auto csv_path = std::filesystem::temp_directory_path() / "rex_perf_counter_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));

  rex::perf::ConfigureCsvLogPathFromCvar();
  rex::perf::SetCounter(rex::perf::CounterId::kFrameTimeUs, 16666);
  rex::perf::SetCounter(rex::perf::CounterId::kDrawCalls, 7);
  rex::perf::IncrementCounter(rex::perf::CounterId::kAsyncPipelineSkippedDraws, 3);
  rex::perf::IncrementCounter(rex::perf::CounterId::kD3D12Submissions, 4);
  rex::perf::IncrementCounter(rex::perf::CounterId::kD3D12PresentCalls, 5);
  rex::perf::IncrementCounter(rex::perf::CounterId::kMemexportReadbackFull, 6);
  rex::perf::IncrementCounter(rex::perf::CounterId::kMemexportReadbackFast, 7);
  rex::perf::IncrementCounter(rex::perf::CounterId::kMemexportReadbackFallback, 8);
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();

  rex::perf::SetCounter(rex::perf::CounterId::kFrameTimeUs, 33333);
  rex::perf::SetCounter(rex::perf::CounterId::kDrawCalls, 2);
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();
  rex::perf::FlushCsv();

  std::ifstream csv(csv_path);
  REQUIRE(csv.is_open());

  std::string header;
  std::string frame0;
  std::string frame1;
  REQUIRE(std::getline(csv, header));
  REQUIRE(std::getline(csv, frame0));
  REQUIRE(std::getline(csv, frame1));

  auto header_values = SplitCsvRow(header);
  auto frame0_values = SplitCsvRow(frame0);
  auto frame1_values = SplitCsvRow(frame1);

  REQUIRE(header_values.size() >= 5);
  REQUIRE(frame0_values.size() == header_values.size());
  REQUIRE(frame1_values.size() == header_values.size());

  CHECK(header_values[0] == "frame_index");
  CHECK(header_values[1] == "elapsed_us");
  CHECK(header_values[2] == "frame_time_us");
  CHECK(header_values[4] == "draw_calls");

  const size_t skipped_draws_col = CsvColumnIndex(header_values, "async_pipeline_skipped_draws");
  const size_t submissions_col = CsvColumnIndex(header_values, "d3d12_submissions");
  const size_t presents_col = CsvColumnIndex(header_values, "d3d12_present_calls");
  const size_t memexport_full_col = CsvColumnIndex(header_values, "memexport_readback_full");
  const size_t memexport_fast_col = CsvColumnIndex(header_values, "memexport_readback_fast");
  const size_t memexport_fallback_col = CsvColumnIndex(header_values, "memexport_readback_fallback");
  REQUIRE(skipped_draws_col != std::string::npos);
  REQUIRE(submissions_col != std::string::npos);
  REQUIRE(presents_col != std::string::npos);
  REQUIRE(memexport_full_col != std::string::npos);
  REQUIRE(memexport_fast_col != std::string::npos);
  REQUIRE(memexport_fallback_col != std::string::npos);

  CHECK(frame0_values[0] == "0");
  CHECK(frame0_values[2] == "16666");
  CHECK(frame0_values[4] == "7");
  CHECK(frame0_values[skipped_draws_col] == "3");
  CHECK(frame0_values[submissions_col] == "4");
  CHECK(frame0_values[presents_col] == "5");
  CHECK(frame0_values[memexport_full_col] == "6");
  CHECK(frame0_values[memexport_fast_col] == "7");
  CHECK(frame0_values[memexport_fallback_col] == "8");

  CHECK(frame1_values[0] == "1");
  CHECK(std::stoull(frame1_values[1]) >= std::stoull(frame0_values[1]));
  CHECK(frame1_values[2] == "33333");
  CHECK(frame1_values[4] == "2");
  CHECK(frame1_values[skipped_draws_col] == "0");
  CHECK(frame1_values[submissions_col] == "0");
  CHECK(frame1_values[presents_col] == "0");
  CHECK(frame1_values[memexport_full_col] == "0");
  CHECK(frame1_values[memexport_fast_col] == "0");
  CHECK(frame1_values[memexport_fallback_col] == "0");
}
