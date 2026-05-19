/**
 * @file        perf_counter_test.cpp
 * @brief       Unit tests for performance counter logging
 *
 * @copyright   Copyright (c) 2026 Tom Clay
 * @license     BSD 3-Clause License
 */

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <rex/cvar.h>
#include <rex/perf/counter.h>

namespace {

struct PerfCsvTestScope {
  explicit PerfCsvTestScope(std::filesystem::path path)
      : old_perf_log_csv(rex::cvar::GetFlagByName("perf_log_csv")),
        old_guest_functions_top_n(rex::cvar::GetFlagByName("perf_guest_functions_top_n")),
        old_guest_functions_min_exclusive_us(
            rex::cvar::GetFlagByName("perf_guest_functions_min_exclusive_us")),
        csv_path(std::move(path)) {
    rex::perf::FlushCsv();
    std::error_code ec;
    std::filesystem::remove(csv_path, ec);
    std::filesystem::remove(GuestFunctionsCsvPath(), ec);
  }

  ~PerfCsvTestScope() {
    rex::perf::FlushCsv();
    rex::cvar::SetFlagByName("perf_log_csv", old_perf_log_csv);
    rex::cvar::SetFlagByName("perf_guest_functions_top_n", old_guest_functions_top_n);
    rex::cvar::SetFlagByName("perf_guest_functions_min_exclusive_us",
                             old_guest_functions_min_exclusive_us);
    std::error_code ec;
    std::filesystem::remove(csv_path, ec);
    std::filesystem::remove(GuestFunctionsCsvPath(), ec);
  }

  std::filesystem::path GuestFunctionsCsvPath() const {
    return std::filesystem::path(csv_path.string() + ".guest_functions.csv");
  }

  std::string old_perf_log_csv;
  std::string old_guest_functions_top_n;
  std::string old_guest_functions_min_exclusive_us;
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
  rex::perf::IncrementCounter(rex::perf::CounterId::kAsyncPipelinePendingDraws, 4);
  rex::perf::IncrementCounter(rex::perf::CounterId::kAsyncPipelineFailedDraws, 5);
  rex::perf::IncrementCounter(rex::perf::CounterId::kD3D12Submissions, 6);
  rex::perf::IncrementCounter(rex::perf::CounterId::kD3D12PresentCalls, 7);
  rex::perf::IncrementCounter(rex::perf::CounterId::kMemexportReadbackFull, 8);
  rex::perf::IncrementCounter(rex::perf::CounterId::kMemexportReadbackFast, 9);
  rex::perf::IncrementCounter(rex::perf::CounterId::kMemexportReadbackFallback, 10);
  rex::perf::IncrementCounter(rex::perf::CounterId::kGuestFunctionDispatchUs, 11);
  rex::perf::IncrementCounter(rex::perf::CounterId::kD3D12SubmissionWaitUs, 12);
  rex::perf::IncrementCounter(rex::perf::CounterId::kD3D12PresentUs, 13);
  rex::perf::IncrementCounter(rex::perf::CounterId::kMemexportReadbackUs, 14);
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
  const size_t pending_draws_col = CsvColumnIndex(header_values, "async_pipeline_pending_draws");
  const size_t failed_draws_col = CsvColumnIndex(header_values, "async_pipeline_failed_draws");
  const size_t submissions_col = CsvColumnIndex(header_values, "d3d12_submissions");
  const size_t presents_col = CsvColumnIndex(header_values, "d3d12_present_calls");
  const size_t memexport_full_col = CsvColumnIndex(header_values, "memexport_readback_full");
  const size_t memexport_fast_col = CsvColumnIndex(header_values, "memexport_readback_fast");
  const size_t memexport_fallback_col = CsvColumnIndex(header_values, "memexport_readback_fallback");
  const size_t guest_dispatch_us_col =
      CsvColumnIndex(header_values, "guest_function_dispatch_us");
  const size_t submission_wait_us_col =
      CsvColumnIndex(header_values, "d3d12_submission_wait_us");
  const size_t present_us_col = CsvColumnIndex(header_values, "d3d12_present_us");
  const size_t memexport_readback_us_col =
      CsvColumnIndex(header_values, "memexport_readback_us");
  REQUIRE(skipped_draws_col != std::string::npos);
  REQUIRE(pending_draws_col != std::string::npos);
  REQUIRE(failed_draws_col != std::string::npos);
  REQUIRE(submissions_col != std::string::npos);
  REQUIRE(presents_col != std::string::npos);
  REQUIRE(memexport_full_col != std::string::npos);
  REQUIRE(memexport_fast_col != std::string::npos);
  REQUIRE(memexport_fallback_col != std::string::npos);
  REQUIRE(guest_dispatch_us_col != std::string::npos);
  REQUIRE(submission_wait_us_col != std::string::npos);
  REQUIRE(present_us_col != std::string::npos);
  REQUIRE(memexport_readback_us_col != std::string::npos);

  CHECK(frame0_values[0] == "0");
  CHECK(frame0_values[2] == "16666");
  CHECK(frame0_values[4] == "7");
  CHECK(frame0_values[skipped_draws_col] == "3");
  CHECK(frame0_values[pending_draws_col] == "4");
  CHECK(frame0_values[failed_draws_col] == "5");
  CHECK(frame0_values[submissions_col] == "6");
  CHECK(frame0_values[presents_col] == "7");
  CHECK(frame0_values[memexport_full_col] == "8");
  CHECK(frame0_values[memexport_fast_col] == "9");
  CHECK(frame0_values[memexport_fallback_col] == "10");
  CHECK(frame0_values[guest_dispatch_us_col] == "11");
  CHECK(frame0_values[submission_wait_us_col] == "12");
  CHECK(frame0_values[present_us_col] == "13");
  CHECK(frame0_values[memexport_readback_us_col] == "14");

  CHECK(frame1_values[0] == "1");
  CHECK(std::stoull(frame1_values[1]) >= std::stoull(frame0_values[1]));
  CHECK(frame1_values[2] == "33333");
  CHECK(frame1_values[4] == "2");
  CHECK(frame1_values[skipped_draws_col] == "0");
  CHECK(frame1_values[pending_draws_col] == "0");
  CHECK(frame1_values[failed_draws_col] == "0");
  CHECK(frame1_values[submissions_col] == "0");
  CHECK(frame1_values[presents_col] == "0");
  CHECK(frame1_values[memexport_full_col] == "0");
  CHECK(frame1_values[memexport_fast_col] == "0");
  CHECK(frame1_values[memexport_fallback_col] == "0");
  CHECK(frame1_values[guest_dispatch_us_col] == "0");
  CHECK(frame1_values[submission_wait_us_col] == "0");
  CHECK(frame1_values[present_us_col] == "0");
  CHECK(frame1_values[memexport_readback_us_col] == "0");
}

TEST_CASE("guest function profile aggregates top exclusive durations", "[perf][counter]") {
  rex::perf::Init();

  rex::perf::AddGuestFunctionDurationUs(0x82220000, "sub_82220000", 100, 70);
  rex::perf::AddGuestFunctionDurationUs(0x82220000, "sub_82220000", 90, 60);
  rex::perf::AddGuestFunctionDurationUs(0x82230000, "sub_82230000", 400, 20);
  rex::perf::AddGuestFunctionDurationUs(0x82240000, "sub_82240000", 50, 50);

  auto entries =
      rex::perf::SnapshotGuestFunctionProfile(/*max_entries=*/2, /*min_exclusive_us=*/1);

  REQUIRE(entries.size() == 2);
  CHECK(entries[0].address == 0x82220000);
  CHECK(entries[0].symbol == "sub_82220000");
  CHECK(entries[0].calls == 2);
  CHECK(entries[0].inclusive_us == 190);
  CHECK(entries[0].exclusive_us == 130);

  CHECK(entries[1].address == 0x82240000);
  CHECK(entries[1].symbol == "sub_82240000");
  CHECK(entries[1].calls == 1);
  CHECK(entries[1].inclusive_us == 50);
  CHECK(entries[1].exclusive_us == 50);

  CHECK(rex::perf::SnapshotGuestFunctionProfile(
            /*max_entries=*/8, /*min_exclusive_us=*/0)
            .empty());
}

TEST_CASE("guest function scope records nested samples when enabled", "[perf][counter]") {
  auto csv_path = std::filesystem::temp_directory_path() / "rex_perf_guest_scope_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));
  REQUIRE(rex::cvar::SetFlagByName("perf_guest_functions_top_n", "4"));

  rex::perf::ConfigureCsvLogPathFromCvar();
  {
    rex::perf::ScopedGuestFunctionProfile outer(0x82220000, "sub_82220000");
    {
      rex::perf::ScopedGuestFunctionProfile inner(0x82230000, "sub_82230000");
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

  auto entries =
      rex::perf::SnapshotGuestFunctionProfile(/*max_entries=*/4, /*min_exclusive_us=*/0);
  REQUIRE(entries.size() == 2);

  const auto find_entry = [&](uint32_t address) -> const rex::perf::GuestFunctionProfileEntry* {
    for (const auto& entry : entries) {
      if (entry.address == address) {
        return &entry;
      }
    }
    return nullptr;
  };

  const auto* outer = find_entry(0x82220000);
  const auto* inner = find_entry(0x82230000);
  REQUIRE(outer != nullptr);
  REQUIRE(inner != nullptr);
  CHECK(outer->symbol == "sub_82220000");
  CHECK(inner->symbol == "sub_82230000");
  CHECK(outer->calls == 1);
  CHECK(inner->calls == 1);
  CHECK(outer->inclusive_us >= outer->exclusive_us);
  CHECK(outer->inclusive_us > outer->exclusive_us);
  CHECK(inner->inclusive_us >= inner->exclusive_us);
}

TEST_CASE("guest function scope is inert by default", "[perf][counter]") {
  auto csv_path = std::filesystem::temp_directory_path() / "rex_perf_guest_default_off_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));

  rex::perf::ConfigureCsvLogPathFromCvar();
  {
    rex::perf::ScopedGuestFunctionProfile guest(0x82220000, "sub_82220000");
  }
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();
  rex::perf::FlushCsv();

  CHECK_FALSE(std::filesystem::exists(scope.GuestFunctionsCsvPath()));
  CHECK(rex::perf::SnapshotGuestFunctionProfile(
            /*max_entries=*/4, /*min_exclusive_us=*/0)
            .empty());
}

TEST_CASE("guest function scope drops live samples when csv closes", "[perf][counter]") {
  auto csv_path = std::filesystem::temp_directory_path() / "rex_perf_guest_live_close_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));
  REQUIRE(rex::cvar::SetFlagByName("perf_guest_functions_top_n", "4"));

  rex::perf::ConfigureCsvLogPathFromCvar();
  {
    rex::perf::ScopedGuestFunctionProfile guest(0x82220000, "sub_82220000");
    rex::perf::FlushCsv();
  }

  CHECK(rex::perf::SnapshotGuestFunctionProfile(
            /*max_entries=*/4, /*min_exclusive_us=*/0)
            .empty());
}

TEST_CASE("perf_log_csv writes guest function sidecar when enabled", "[perf][counter]") {
  auto csv_path = std::filesystem::temp_directory_path() / "rex_perf_guest_function_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));
  REQUIRE(rex::cvar::SetFlagByName("perf_guest_functions_top_n", "2"));
  REQUIRE(rex::cvar::SetFlagByName("perf_guest_functions_min_exclusive_us", "25"));

  rex::perf::ConfigureCsvLogPathFromCvar();
  rex::perf::AddGuestFunctionDurationUs(0x82220000, "sub_82220000", 100, 70);
  rex::perf::AddGuestFunctionDurationUs(0x82230000, "sub_82230000", 200, 20);
  rex::perf::AddGuestFunctionDurationUs(0x82240000, "sub_82240000", 80, 60);
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();
  rex::perf::FlushCsv();

  std::ifstream guest_csv(scope.GuestFunctionsCsvPath());
  REQUIRE(guest_csv.is_open());

  std::string header;
  std::string rank0;
  std::string rank1;
  REQUIRE(std::getline(guest_csv, header));
  REQUIRE(std::getline(guest_csv, rank0));
  REQUIRE(std::getline(guest_csv, rank1));

  CHECK(header == "frame_index,elapsed_us,rank,guest_address,symbol,calls,inclusive_us,exclusive_us");

  auto rank0_values = SplitCsvRow(rank0);
  auto rank1_values = SplitCsvRow(rank1);
  REQUIRE(rank0_values.size() == 8);
  REQUIRE(rank1_values.size() == 8);

  CHECK(rank0_values[0] == "0");
  CHECK(rank0_values[2] == "1");
  CHECK(rank0_values[3] == "0x82220000");
  CHECK(rank0_values[4] == "sub_82220000");
  CHECK(rank0_values[5] == "1");
  CHECK(rank0_values[6] == "100");
  CHECK(rank0_values[7] == "70");

  CHECK(rank1_values[2] == "2");
  CHECK(rank1_values[3] == "0x82240000");
  CHECK(rank1_values[7] == "60");
}

TEST_CASE("perf_log_csv can enable guest function sidecar after csv startup", "[perf][counter]") {
  auto csv_path = std::filesystem::temp_directory_path() / "rex_perf_guest_function_toggle_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));

  rex::perf::ConfigureCsvLogPathFromCvar();
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();
  CHECK_FALSE(std::filesystem::exists(scope.GuestFunctionsCsvPath()));

  REQUIRE(rex::cvar::SetFlagByName("perf_guest_functions_top_n", "1"));
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();
  rex::perf::AddGuestFunctionDurationUs(0x82220000, "sub_82220000", 100, 70);
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();
  rex::perf::FlushCsv();

  std::ifstream guest_csv(scope.GuestFunctionsCsvPath());
  REQUIRE(guest_csv.is_open());

  std::string header;
  std::string profiled_frame;
  REQUIRE(std::getline(guest_csv, header));
  REQUIRE(std::getline(guest_csv, profiled_frame));

  auto values = SplitCsvRow(profiled_frame);
  REQUIRE(values.size() == 8);
  CHECK(values[2] == "1");
  CHECK(values[3] == "0x82220000");
  CHECK(values[7] == "70");
}

TEST_CASE("perf_log_csv escapes guest function symbols in sidecar", "[perf][counter]") {
  auto csv_path = std::filesystem::temp_directory_path() / "rex_perf_guest_function_escape_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));
  REQUIRE(rex::cvar::SetFlagByName("perf_guest_functions_top_n", "1"));

  rex::perf::ConfigureCsvLogPathFromCvar();
  rex::perf::AddGuestFunctionDurationUs(0x82220000, "sub,quo\"te", 100, 70);
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();
  rex::perf::FlushCsv();

  std::ifstream guest_csv(scope.GuestFunctionsCsvPath());
  REQUIRE(guest_csv.is_open());

  std::string header;
  std::string row;
  REQUIRE(std::getline(guest_csv, header));
  REQUIRE(std::getline(guest_csv, row));

  CHECK(row.find("0,") == 0);
  CHECK(row.find(",1,0x82220000,\"sub,quo\"\"te\",1,100,70") != std::string::npos);
}
