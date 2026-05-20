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
        old_guest_indirect_targets_top_n(
            rex::cvar::GetFlagByName("perf_guest_indirect_targets_top_n")),
        csv_path(std::move(path)) {
    rex::perf::FlushCsv();
    rex::perf::Init();
    std::error_code ec;
    std::filesystem::remove(csv_path, ec);
    std::filesystem::remove(GuestFunctionsCsvPath(), ec);
    std::filesystem::remove(GuestFunctionsSummaryCsvPath(), ec);
    std::filesystem::remove(GuestIndirectTargetsCsvPath(), ec);
    std::filesystem::remove(GuestIndirectTargetsSummaryCsvPath(), ec);
  }

  ~PerfCsvTestScope() {
    rex::perf::FlushCsv();
    rex::cvar::SetFlagByName("perf_log_csv", old_perf_log_csv);
    rex::cvar::SetFlagByName("perf_guest_functions_top_n", old_guest_functions_top_n);
    rex::cvar::SetFlagByName("perf_guest_functions_min_exclusive_us",
                             old_guest_functions_min_exclusive_us);
    rex::cvar::SetFlagByName("perf_guest_indirect_targets_top_n",
                             old_guest_indirect_targets_top_n);
    std::error_code ec;
    std::filesystem::remove(csv_path, ec);
    std::filesystem::remove(GuestFunctionsCsvPath(), ec);
    std::filesystem::remove(GuestFunctionsSummaryCsvPath(), ec);
    std::filesystem::remove(GuestIndirectTargetsCsvPath(), ec);
    std::filesystem::remove(GuestIndirectTargetsSummaryCsvPath(), ec);
  }

  std::filesystem::path GuestFunctionsCsvPath() const {
    return std::filesystem::path(csv_path.string() + ".guest_functions.csv");
  }

  std::filesystem::path GuestFunctionsSummaryCsvPath() const {
    return std::filesystem::path(csv_path.string() + ".guest_functions.summary.csv");
  }

  std::filesystem::path GuestIndirectTargetsCsvPath() const {
    return std::filesystem::path(csv_path.string() + ".guest_indirect_targets.csv");
  }

  std::filesystem::path GuestIndirectTargetsSummaryCsvPath() const {
    return std::filesystem::path(csv_path.string() + ".guest_indirect_targets.summary.csv");
  }

  std::string old_perf_log_csv;
  std::string old_guest_functions_top_n;
  std::string old_guest_functions_min_exclusive_us;
  std::string old_guest_indirect_targets_top_n;
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
  rex::perf::IncrementCounter(rex::perf::CounterId::kGuestKernelWaitUs, 12);
  rex::perf::IncrementCounter(rex::perf::CounterId::kD3D12SubmissionWaitUs, 13);
  rex::perf::IncrementCounter(rex::perf::CounterId::kD3D12PresentUs, 14);
  rex::perf::IncrementCounter(rex::perf::CounterId::kMemexportReadbackUs, 15);
  rex::perf::IncrementCounter(rex::perf::CounterId::kAudioSilenceFrames, 3);
  rex::perf::IncrementCounter(rex::perf::CounterId::kAudioStartupSilenceFrames, 1);
  rex::perf::IncrementCounter(rex::perf::CounterId::kAudioUnderrunFrames, 2);
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
  const size_t guest_wait_us_col = CsvColumnIndex(header_values, "guest_kernel_wait_us");
  const size_t submission_wait_us_col =
      CsvColumnIndex(header_values, "d3d12_submission_wait_us");
  const size_t present_us_col = CsvColumnIndex(header_values, "d3d12_present_us");
  const size_t memexport_readback_us_col =
      CsvColumnIndex(header_values, "memexport_readback_us");
  const size_t audio_silence_frames_col =
      CsvColumnIndex(header_values, "audio_silence_frames");
  const size_t audio_startup_silence_frames_col =
      CsvColumnIndex(header_values, "audio_startup_silence_frames");
  const size_t audio_underrun_frames_col =
      CsvColumnIndex(header_values, "audio_underrun_frames");
  REQUIRE(skipped_draws_col != std::string::npos);
  REQUIRE(pending_draws_col != std::string::npos);
  REQUIRE(failed_draws_col != std::string::npos);
  REQUIRE(submissions_col != std::string::npos);
  REQUIRE(presents_col != std::string::npos);
  REQUIRE(memexport_full_col != std::string::npos);
  REQUIRE(memexport_fast_col != std::string::npos);
  REQUIRE(memexport_fallback_col != std::string::npos);
  REQUIRE(guest_dispatch_us_col != std::string::npos);
  REQUIRE(guest_wait_us_col != std::string::npos);
  REQUIRE(submission_wait_us_col != std::string::npos);
  REQUIRE(present_us_col != std::string::npos);
  REQUIRE(memexport_readback_us_col != std::string::npos);
  REQUIRE(audio_silence_frames_col != std::string::npos);
  REQUIRE(audio_startup_silence_frames_col != std::string::npos);
  REQUIRE(audio_underrun_frames_col != std::string::npos);

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
  CHECK(frame0_values[guest_wait_us_col] == "12");
  CHECK(frame0_values[submission_wait_us_col] == "13");
  CHECK(frame0_values[present_us_col] == "14");
  CHECK(frame0_values[memexport_readback_us_col] == "15");
  CHECK(frame0_values[audio_silence_frames_col] == "3");
  CHECK(frame0_values[audio_startup_silence_frames_col] == "1");
  CHECK(frame0_values[audio_underrun_frames_col] == "2");

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
  CHECK(frame1_values[guest_wait_us_col] == "0");
  CHECK(frame1_values[submission_wait_us_col] == "0");
  CHECK(frame1_values[present_us_col] == "0");
  CHECK(frame1_values[memexport_readback_us_col] == "0");
  CHECK(frame1_values[audio_silence_frames_col] == "0");
  CHECK(frame1_values[audio_startup_silence_frames_col] == "0");
  CHECK(frame1_values[audio_underrun_frames_col] == "0");
}

TEST_CASE("buffer queue depth persists as a gauge until it is updated", "[perf][counter]") {
  rex::perf::Init();

  rex::perf::SetCounter(rex::perf::CounterId::kBufferQueueDepth, 3);
  rex::perf::ResetFrameCounters();
  CHECK(rex::perf::GetSnapshotCounter(rex::perf::CounterId::kBufferQueueDepth) == 3);

  rex::perf::ResetFrameCounters();
  CHECK(rex::perf::GetSnapshotCounter(rex::perf::CounterId::kBufferQueueDepth) == 3);

  rex::perf::SetCounter(rex::perf::CounterId::kBufferQueueDepth, 0);
  rex::perf::ResetFrameCounters();
  CHECK(rex::perf::GetSnapshotCounter(rex::perf::CounterId::kBufferQueueDepth) == 0);
}

TEST_CASE("guest function profile aggregates top active exclusive durations", "[perf][counter]") {
  rex::perf::Init();

  rex::perf::AddGuestFunctionDurationUs(0x82220000, "sub_82220000", 100, 70, 65);
  rex::perf::AddGuestFunctionDurationUs(0x82220000, "sub_82220000", 90, 60, 55);
  rex::perf::AddGuestFunctionDurationUs(0x82230000, "sub_82230000", 400, 20);
  rex::perf::AddGuestFunctionDurationUs(0x82240000, "sub_82240000", 50, 50, 0, 3);

  auto entries =
      rex::perf::SnapshotGuestFunctionProfile(/*max_entries=*/2, /*min_exclusive_us=*/1);

  REQUIRE(entries.size() == 2);
  CHECK(entries[0].address == 0x82240000);
  CHECK(entries[0].symbol == "sub_82240000");
  CHECK(entries[0].calls == 1);
  CHECK(entries[0].inclusive_us == 50);
  CHECK(entries[0].exclusive_us == 50);
  CHECK(entries[0].blocking_wait_us == 0);
  CHECK(entries[0].active_exclusive_us == 50);
  CHECK(entries[0].static_spin_hint_sites == 3);
  CHECK(entries[0].dynamic_spin_hint_executions == 0);

  CHECK(entries[1].address == 0x82230000);
  CHECK(entries[1].symbol == "sub_82230000");
  CHECK(entries[1].calls == 1);
  CHECK(entries[1].inclusive_us == 400);
  CHECK(entries[1].exclusive_us == 20);
  CHECK(entries[1].blocking_wait_us == 0);
  CHECK(entries[1].active_exclusive_us == 20);
  CHECK(entries[1].static_spin_hint_sites == 0);
  CHECK(entries[1].dynamic_spin_hint_executions == 0);

  CHECK(rex::perf::SnapshotGuestFunctionProfile(
            /*max_entries=*/8, /*min_exclusive_us=*/0)
            .empty());
}

TEST_CASE("guest function profile macro accepts legacy and spin-site forms", "[perf][counter]") {
  auto csv_path = std::filesystem::temp_directory_path() / "rex_perf_guest_function_macro_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));
  REQUIRE(rex::cvar::SetFlagByName("perf_guest_functions_top_n", "4"));

  rex::perf::ConfigureCsvLogPathFromCvar();
  {
    PROFILE_GUEST_FUNCTION_SCOPE(0x82220000, "legacy_form");
  }
  {
    PROFILE_GUEST_FUNCTION_SCOPE(0x82230000, "spin_form", 7);
  }
  SUCCEED("both macro forms compile");
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

TEST_CASE("guest function scope separates blocking wait from active exclusive time",
          "[perf][counter]") {
  auto csv_path = std::filesystem::temp_directory_path() / "rex_perf_guest_wait_scope_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));
  REQUIRE(rex::cvar::SetFlagByName("perf_guest_functions_top_n", "4"));

  rex::perf::ConfigureCsvLogPathFromCvar();
  {
    rex::perf::ScopedGuestFunctionProfile guest(0x82220000, "sub_82220000");
    rex::perf::AddGuestKernelWaitDurationUs(25);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  auto entries =
      rex::perf::SnapshotGuestFunctionProfile(/*max_entries=*/4, /*min_exclusive_us=*/0);
  REQUIRE(entries.size() == 1);
  CHECK(entries[0].address == 0x82220000);
  CHECK(entries[0].blocking_wait_us == 25);
  CHECK(entries[0].exclusive_us >= entries[0].blocking_wait_us);
  CHECK(entries[0].active_exclusive_us == entries[0].exclusive_us - entries[0].blocking_wait_us);
  CHECK(rex::perf::GetCounter(rex::perf::CounterId::kGuestKernelWaitUs) == 25);
}

TEST_CASE("guest function scope counts dynamic spin hint executions", "[perf][counter]") {
  auto csv_path = std::filesystem::temp_directory_path() / "rex_perf_guest_spin_scope_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));
  REQUIRE(rex::cvar::SetFlagByName("perf_guest_functions_top_n", "4"));

  rex::perf::ConfigureCsvLogPathFromCvar();
  {
    rex::perf::ScopedGuestFunctionProfile outer(0x82220000, "sub_82220000", 2);
    rex::perf::AddGuestSpinHintExecution();
    {
      rex::perf::ScopedGuestFunctionProfile inner(0x82230000, "sub_82230000", 1);
      rex::perf::AddGuestSpinHintExecution();
      rex::perf::AddGuestSpinHintExecution();
    }
    rex::perf::AddGuestSpinHintExecutions(3);
    rex::perf::AddGuestSpinHintExecution();
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
  CHECK(outer->static_spin_hint_sites == 2);
  CHECK(outer->dynamic_spin_hint_executions == 5);
  CHECK(inner->static_spin_hint_sites == 1);
  CHECK(inner->dynamic_spin_hint_executions == 2);
}

TEST_CASE("guest function scope is inert by default", "[perf][counter]") {
  auto csv_path = std::filesystem::temp_directory_path() / "rex_perf_guest_default_off_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));

  rex::perf::ConfigureCsvLogPathFromCvar();
  {
    rex::perf::ScopedGuestFunctionProfile guest(0x82220000, "sub_82220000");
  }
  rex::perf::AddGuestIndirectCallTarget(0x82220000, "sub_82220000", 0x82220010, 0x82300000,
                                        true);
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();
  rex::perf::FlushCsv();

  CHECK_FALSE(std::filesystem::exists(scope.GuestFunctionsCsvPath()));
  CHECK_FALSE(std::filesystem::exists(scope.GuestIndirectTargetsCsvPath()));
  CHECK(rex::perf::SnapshotGuestFunctionProfile(
            /*max_entries=*/4, /*min_exclusive_us=*/0)
            .empty());
  CHECK(rex::perf::SnapshotGuestIndirectCallTargetProfile(/*max_entries=*/4).empty());
}

TEST_CASE("guest indirect target profile aggregates current generated source",
          "[perf][counter]") {
  auto csv_path =
      std::filesystem::temp_directory_path() / "rex_perf_guest_indirect_target_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));
  REQUIRE(rex::cvar::SetFlagByName("perf_guest_indirect_targets_top_n", "4"));

  rex::perf::ConfigureCsvLogPathFromCvar();
  rex::perf::AddGuestIndirectCallTarget(0x82220000, "sub,source", 0x82220010, 0x82300000,
                                        "sub,target", true);
  rex::perf::AddGuestIndirectCallTarget(0x82220000, "sub,source", 0x82220010, 0x82300000,
                                        "sub,target", false);
  rex::perf::AddGuestIndirectCallTarget(0x82220000, "sub,source", 0x82220014, 0x82400000,
                                        "sub_82400000", false);
  rex::perf::AddGuestIndirectCallTarget(0x82230000, "sub_82230000", 0x82230008, 0x82500000,
                                        true);

  auto entries = rex::perf::SnapshotGuestIndirectCallTargetProfile(/*max_entries=*/4);
  REQUIRE(entries.size() == 3);
  CHECK(entries[0].source_address == 0x82220000);
  CHECK(entries[0].source_symbol == "sub,source");
  CHECK(entries[0].call_site == 0x82220010);
  CHECK(entries[0].target_address == 0x82300000);
  CHECK(entries[0].target_symbol == "sub,target");
  CHECK(entries[0].calls == 2);
  CHECK(entries[0].fast_path_hits == 1);
  CHECK(entries[0].fallback_hits == 1);

  CHECK(entries[1].source_address == 0x82220000);
  CHECK(entries[1].call_site == 0x82220014);
  CHECK(entries[1].target_address == 0x82400000);
  CHECK(entries[1].target_symbol == "sub_82400000");
  CHECK(entries[1].calls == 1);
  CHECK(entries[1].fast_path_hits == 0);
  CHECK(entries[1].fallback_hits == 1);

  CHECK(entries[2].source_address == 0x82230000);
  CHECK(entries[2].call_site == 0x82230008);
  CHECK(entries[2].target_address == 0x82500000);
  CHECK(entries[2].target_symbol == "sub_82500000");
  CHECK(entries[2].calls == 1);
  CHECK(entries[2].fast_path_hits == 1);
  CHECK(entries[2].fallback_hits == 0);

  CHECK(rex::perf::SnapshotGuestIndirectCallTargetProfile(/*max_entries=*/4).empty());
}

TEST_CASE("perf_log_csv writes guest indirect target sidecar when enabled",
          "[perf][counter]") {
  auto csv_path =
      std::filesystem::temp_directory_path() / "rex_perf_guest_indirect_target_csv_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));
  REQUIRE(rex::cvar::SetFlagByName("perf_guest_indirect_targets_top_n", "2"));

  rex::perf::ConfigureCsvLogPathFromCvar();
  rex::perf::AddGuestIndirectCallTarget(0x82220000, "sub,source", 0x82220010, 0x82300000,
                                        "sub,target", true);
  rex::perf::AddGuestIndirectCallTarget(0x82220000, "sub,source", 0x82220010, 0x82300000,
                                        "sub,target", false);
  rex::perf::AddGuestIndirectCallTarget(0x82220000, "sub,source", 0x82220014, 0x82400000,
                                        "sub_82400000", false);
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();
  rex::perf::FlushCsv();

  std::ifstream indirect_csv(scope.GuestIndirectTargetsCsvPath());
  REQUIRE(indirect_csv.is_open());

  std::string header;
  std::string rank0;
  std::string rank1;
  REQUIRE(std::getline(indirect_csv, header));
  REQUIRE(std::getline(indirect_csv, rank0));
  REQUIRE(std::getline(indirect_csv, rank1));

  CHECK(header ==
        "frame_index,elapsed_us,rank,source_guest_address,source_symbol,call_site,"
        "call_site_symbol,target_guest_address,target_symbol,calls,fast_path_hits,fallback_hits");

  CHECK(rank0.find("0,") == 0);
  CHECK(rank0.find("1,0x82220000,\"sub,source\",0x82220010,\"sub,source+0x10\",0x82300000,"
                   "\"sub,target\",2,1,1") !=
        std::string::npos);
  CHECK(rank1.find("2,0x82220000,\"sub,source\",0x82220014,\"sub,source+0x14\",0x82400000,"
                   "sub_82400000,1,0,1") !=
        std::string::npos);
}

TEST_CASE("perf_log_csv writes aggregate guest indirect target summary sidecar when enabled",
          "[perf][counter]") {
  auto csv_path =
      std::filesystem::temp_directory_path() / "rex_perf_guest_indirect_target_summary_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));
  REQUIRE(rex::cvar::SetFlagByName("perf_guest_indirect_targets_top_n", "2"));

  rex::perf::ConfigureCsvLogPathFromCvar();
  rex::perf::AddGuestIndirectCallTarget(0x82220000, "sub_82220000", 0x82220010,
                                        0x82300000, true);
  rex::perf::AddGuestIndirectCallTarget(0x82220000, "sub_82220000", 0x82220010,
                                        0x82300000, false);
  rex::perf::AddGuestIndirectCallTarget(0x82220000, "sub_82220000", 0x82220014,
                                        0x82400000, false);
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();

  rex::perf::AddGuestIndirectCallTarget(0x82220000, "sub_82220000", 0x82220010,
                                        0x82300000, true);
  rex::perf::AddGuestIndirectCallTarget(0x82230000, "sub_82230000", 0x82230008,
                                        0x82500000, false);
  rex::perf::AddGuestIndirectCallTarget(0x82230000, "sub_82230000", 0x82230008,
                                        0x82500000, false);
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();
  rex::perf::FlushCsv();

  std::ifstream summary_csv(scope.GuestIndirectTargetsSummaryCsvPath());
  REQUIRE(summary_csv.is_open());

  std::string header;
  std::string rank0;
  std::string rank1;
  REQUIRE(std::getline(summary_csv, header));
  REQUIRE(std::getline(summary_csv, rank0));
  REQUIRE(std::getline(summary_csv, rank1));

  CHECK(header ==
        "rank,source_guest_address,source_symbol,call_site,call_site_symbol,"
        "target_guest_address,target_symbol,calls,fast_path_hits,fallback_hits,"
        "fast_path_hits_per_call,fallback_hits_per_call");

  const auto rank0_values = SplitCsvRow(rank0);
  const auto rank1_values = SplitCsvRow(rank1);
  REQUIRE(rank0_values.size() == 12);
  REQUIRE(rank1_values.size() == 12);

  CHECK(rank0_values[0] == "1");
  CHECK(rank0_values[1] == "0x82220000");
  CHECK(rank0_values[2] == "sub_82220000");
  CHECK(rank0_values[3] == "0x82220010");
  CHECK(rank0_values[4] == "sub_82220000+0x10");
  CHECK(rank0_values[5] == "0x82300000");
  CHECK(rank0_values[6] == "sub_82300000");
  CHECK(rank0_values[7] == "3");
  CHECK(rank0_values[8] == "2");
  CHECK(rank0_values[9] == "1");
  CHECK(rank0_values[10] == "0.667");
  CHECK(rank0_values[11] == "0.333");

  CHECK(rank1_values[0] == "2");
  CHECK(rank1_values[1] == "0x82230000");
  CHECK(rank1_values[2] == "sub_82230000");
  CHECK(rank1_values[3] == "0x82230008");
  CHECK(rank1_values[4] == "sub_82230000+0x8");
  CHECK(rank1_values[5] == "0x82500000");
  CHECK(rank1_values[6] == "sub_82500000");
  CHECK(rank1_values[7] == "2");
  CHECK(rank1_values[8] == "0");
  CHECK(rank1_values[9] == "2");
  CHECK(rank1_values[10] == "0.000");
  CHECK(rank1_values[11] == "1.000");
}

TEST_CASE("perf_log_csv writes guest indirect target summary beside previous csv when disabled",
          "[perf][counter]") {
  auto csv_path =
      std::filesystem::temp_directory_path() /
      "rex_perf_guest_indirect_target_summary_disable_test.csv";
  PerfCsvTestScope scope(csv_path);
  const auto cwd_summary_path = std::filesystem::path(".guest_indirect_targets.summary.csv");
  std::error_code ec;
  std::filesystem::remove(cwd_summary_path, ec);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));
  REQUIRE(rex::cvar::SetFlagByName("perf_guest_indirect_targets_top_n", "1"));

  rex::perf::ConfigureCsvLogPathFromCvar();
  rex::perf::AddGuestIndirectCallTarget(0x82220000, "sub_82220000", 0x82220010,
                                        0x82300000, false);
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", ""));
  rex::perf::ConfigureCsvLogPathFromCvar();

  CHECK_FALSE(std::filesystem::exists(cwd_summary_path));

  std::ifstream summary_csv(scope.GuestIndirectTargetsSummaryCsvPath());
  REQUIRE(summary_csv.is_open());

  std::string header;
  std::string rank0;
  REQUIRE(std::getline(summary_csv, header));
  REQUIRE(std::getline(summary_csv, rank0));

  CHECK(header ==
        "rank,source_guest_address,source_symbol,call_site,call_site_symbol,"
        "target_guest_address,target_symbol,calls,fast_path_hits,fallback_hits,"
        "fast_path_hits_per_call,fallback_hits_per_call");

  const auto rank0_values = SplitCsvRow(rank0);
  REQUIRE(rank0_values.size() == 12);
  CHECK(rank0_values[0] == "1");
  CHECK(rank0_values[1] == "0x82220000");
  CHECK(rank0_values[7] == "1");
  CHECK(rank0_values[8] == "0");
  CHECK(rank0_values[9] == "1");
  CHECK(rank0_values[10] == "0.000");
  CHECK(rank0_values[11] == "1.000");

  std::filesystem::remove(cwd_summary_path, ec);
}

TEST_CASE("perf_log_csv refreshes guest indirect target summary during periodic flush",
          "[perf][counter]") {
  auto csv_path =
      std::filesystem::temp_directory_path() /
      "rex_perf_guest_indirect_target_summary_live_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));
  REQUIRE(rex::cvar::SetFlagByName("perf_guest_indirect_targets_top_n", "1"));

  rex::perf::ConfigureCsvLogPathFromCvar();
  for (int frame = 0; frame < 60; ++frame) {
    rex::perf::AddGuestIndirectCallTarget(0x82220000, "sub_82220000", 0x82220010,
                                          0x82300000, true);
    rex::perf::ResetFrameCounters();
    rex::perf::WriteCsvFrame();
  }

  std::ifstream summary_csv(scope.GuestIndirectTargetsSummaryCsvPath());
  REQUIRE(summary_csv.is_open());

  std::string header;
  std::string rank0;
  REQUIRE(std::getline(summary_csv, header));
  REQUIRE(std::getline(summary_csv, rank0));

  const auto rank0_values = SplitCsvRow(rank0);
  REQUIRE(rank0_values.size() == 12);
  CHECK(rank0_values[0] == "1");
  CHECK(rank0_values[1] == "0x82220000");
  CHECK(rank0_values[7] == "60");
  CHECK(rank0_values[8] == "60");
  CHECK(rank0_values[9] == "0");
  CHECK(rank0_values[10] == "1.000");
  CHECK(rank0_values[11] == "0.000");
}

TEST_CASE("perf_log_csv can enable guest indirect target sidecar after csv startup",
          "[perf][counter]") {
  auto csv_path =
      std::filesystem::temp_directory_path() / "rex_perf_guest_indirect_target_toggle_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));

  rex::perf::ConfigureCsvLogPathFromCvar();
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();
  CHECK_FALSE(std::filesystem::exists(scope.GuestIndirectTargetsCsvPath()));

  REQUIRE(rex::cvar::SetFlagByName("perf_guest_indirect_targets_top_n", "1"));
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();
  rex::perf::AddGuestIndirectCallTarget(0x82220000, "sub_82220000", 0x82220010,
                                        0x82300000, false);
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();
  rex::perf::FlushCsv();

  std::ifstream indirect_csv(scope.GuestIndirectTargetsCsvPath());
  REQUIRE(indirect_csv.is_open());

  std::string header;
  std::string profiled_frame;
  REQUIRE(std::getline(indirect_csv, header));
  REQUIRE(std::getline(indirect_csv, profiled_frame));

  CHECK(profiled_frame.find("1,0x82220000,sub_82220000,0x82220010,"
                            "sub_82220000+0x10,0x82300000,"
                            "sub_82300000,1,0,1") != std::string::npos);
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
  rex::perf::AddGuestFunctionDurationUs(0x82220000, "sub_82220000", 100, 70, 0, 4);
  rex::perf::AddGuestFunctionDurationUs(0x82230000, "sub_82230000", 200, 20);
  rex::perf::AddGuestFunctionDurationUs(0x82240000, "sub_82240000", 80, 60, 0, 3);
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

  CHECK(header ==
        "frame_index,elapsed_us,rank,guest_address,symbol,calls,inclusive_us,exclusive_us,"
        "blocking_wait_us,active_exclusive_us,static_spin_hint_sites,"
        "dynamic_spin_hint_executions");

  auto rank0_values = SplitCsvRow(rank0);
  auto rank1_values = SplitCsvRow(rank1);
  REQUIRE(rank0_values.size() == 12);
  REQUIRE(rank1_values.size() == 12);

  CHECK(rank0_values[0] == "0");
  CHECK(rank0_values[2] == "1");
  CHECK(rank0_values[3] == "0x82220000");
  CHECK(rank0_values[4] == "sub_82220000");
  CHECK(rank0_values[5] == "1");
  CHECK(rank0_values[6] == "100");
  CHECK(rank0_values[7] == "70");
  CHECK(rank0_values[8] == "0");
  CHECK(rank0_values[9] == "70");
  CHECK(rank0_values[10] == "4");
  CHECK(rank0_values[11] == "0");

  CHECK(rank1_values[2] == "2");
  CHECK(rank1_values[3] == "0x82240000");
  CHECK(rank1_values[7] == "60");
  CHECK(rank1_values[8] == "0");
  CHECK(rank1_values[9] == "60");
  CHECK(rank1_values[10] == "3");
  CHECK(rank1_values[11] == "0");
}

TEST_CASE("perf_log_csv writes aggregate guest function summary sidecar when enabled",
          "[perf][counter]") {
  auto csv_path =
      std::filesystem::temp_directory_path() / "rex_perf_guest_function_summary_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));
  REQUIRE(rex::cvar::SetFlagByName("perf_guest_functions_top_n", "2"));
  REQUIRE(rex::cvar::SetFlagByName("perf_guest_functions_min_exclusive_us", "25"));

  rex::perf::ConfigureCsvLogPathFromCvar();
  rex::perf::AddGuestFunctionDurationUs(0x82220000, "sub_82220000", 100, 70, 10, 4, 32);
  rex::perf::AddGuestFunctionDurationUs(0x82230000, "sub_82230000", 200, 20);
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();

  rex::perf::AddGuestFunctionDurationUs(0x82220000, "sub_82220000", 80, 50, 5, 4, 64);
  rex::perf::AddGuestFunctionDurationUs(0x82240000, "sub_82240000", 90, 60, 0, 3, 32);
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();
  rex::perf::FlushCsv();

  std::ifstream summary_csv(scope.GuestFunctionsSummaryCsvPath());
  REQUIRE(summary_csv.is_open());

  std::string header;
  std::string rank0;
  std::string rank1;
  REQUIRE(std::getline(summary_csv, header));
  REQUIRE(std::getline(summary_csv, rank0));
  REQUIRE(std::getline(summary_csv, rank1));

  CHECK(header ==
        "rank,guest_address,symbol,calls,inclusive_us,exclusive_us,blocking_wait_us,"
        "active_exclusive_us,static_spin_hint_sites,dynamic_spin_hint_executions,"
        "active_exclusive_us_per_call,dynamic_spin_hint_executions_per_call,"
        "active_exclusive_percent");

  auto rank0_values = SplitCsvRow(rank0);
  auto rank1_values = SplitCsvRow(rank1);
  REQUIRE(rank0_values.size() == 13);
  REQUIRE(rank1_values.size() == 13);

  CHECK(rank0_values[0] == "1");
  CHECK(rank0_values[1] == "0x82220000");
  CHECK(rank0_values[2] == "sub_82220000");
  CHECK(rank0_values[3] == "2");
  CHECK(rank0_values[4] == "180");
  CHECK(rank0_values[5] == "120");
  CHECK(rank0_values[6] == "15");
  CHECK(rank0_values[7] == "105");
  CHECK(rank0_values[8] == "4");
  CHECK(rank0_values[9] == "96");
  CHECK(rank0_values[10] == "52.500");
  CHECK(rank0_values[11] == "48.000");
  CHECK(rank0_values[12] == "63.636");

  CHECK(rank1_values[0] == "2");
  CHECK(rank1_values[1] == "0x82240000");
  CHECK(rank1_values[2] == "sub_82240000");
  CHECK(rank1_values[3] == "1");
  CHECK(rank1_values[4] == "90");
  CHECK(rank1_values[5] == "60");
  CHECK(rank1_values[6] == "0");
  CHECK(rank1_values[7] == "60");
  CHECK(rank1_values[8] == "3");
  CHECK(rank1_values[9] == "32");
  CHECK(rank1_values[10] == "60.000");
  CHECK(rank1_values[11] == "32.000");
  CHECK(rank1_values[12] == "36.364");
}

TEST_CASE("perf_log_csv writes guest function summary beside previous csv when disabled",
          "[perf][counter]") {
  auto csv_path =
      std::filesystem::temp_directory_path() / "rex_perf_guest_function_summary_disable_test.csv";
  PerfCsvTestScope scope(csv_path);
  const auto cwd_summary_path = std::filesystem::path(".guest_functions.summary.csv");
  std::error_code ec;
  std::filesystem::remove(cwd_summary_path, ec);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));
  REQUIRE(rex::cvar::SetFlagByName("perf_guest_functions_top_n", "2"));

  rex::perf::ConfigureCsvLogPathFromCvar();
  rex::perf::AddGuestFunctionDurationUs(0x82220000, "sub_82220000", 100, 70);
  rex::perf::ResetFrameCounters();
  rex::perf::WriteCsvFrame();

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", ""));
  rex::perf::ConfigureCsvLogPathFromCvar();

  CHECK_FALSE(std::filesystem::exists(cwd_summary_path));

  std::ifstream summary_csv(scope.GuestFunctionsSummaryCsvPath());
  REQUIRE(summary_csv.is_open());

  std::string header;
  std::string rank0;
  REQUIRE(std::getline(summary_csv, header));
  REQUIRE(std::getline(summary_csv, rank0));

  CHECK(header ==
        "rank,guest_address,symbol,calls,inclusive_us,exclusive_us,blocking_wait_us,"
        "active_exclusive_us,static_spin_hint_sites,dynamic_spin_hint_executions,"
        "active_exclusive_us_per_call,dynamic_spin_hint_executions_per_call,"
        "active_exclusive_percent");

  auto rank0_values = SplitCsvRow(rank0);
  REQUIRE(rank0_values.size() == 13);
  CHECK(rank0_values[0] == "1");
  CHECK(rank0_values[1] == "0x82220000");
  CHECK(rank0_values[3] == "1");
  CHECK(rank0_values[5] == "70");
  CHECK(rank0_values[7] == "70");
  CHECK(rank0_values[9] == "0");
  CHECK(rank0_values[10] == "70.000");
  CHECK(rank0_values[11] == "0.000");
  CHECK(rank0_values[12] == "100.000");

  std::filesystem::remove(cwd_summary_path, ec);
}

TEST_CASE("perf_log_csv refreshes guest function summary during periodic flush",
          "[perf][counter]") {
  auto csv_path =
      std::filesystem::temp_directory_path() / "rex_perf_guest_function_summary_live_test.csv";
  PerfCsvTestScope scope(csv_path);

  REQUIRE(rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()));
  REQUIRE(rex::cvar::SetFlagByName("perf_guest_functions_top_n", "1"));

  rex::perf::ConfigureCsvLogPathFromCvar();
  for (int frame = 0; frame < 60; ++frame) {
    rex::perf::AddGuestFunctionDurationUs(0x82220000, "sub_82220000", 100, 70);
    rex::perf::ResetFrameCounters();
    rex::perf::WriteCsvFrame();
  }

  std::ifstream summary_csv(scope.GuestFunctionsSummaryCsvPath());
  REQUIRE(summary_csv.is_open());

  std::string header;
  std::string rank0;
  REQUIRE(std::getline(summary_csv, header));
  REQUIRE(std::getline(summary_csv, rank0));

  auto rank0_values = SplitCsvRow(rank0);
  REQUIRE(rank0_values.size() == 13);
  CHECK(rank0_values[0] == "1");
  CHECK(rank0_values[1] == "0x82220000");
  CHECK(rank0_values[3] == "60");
  CHECK(rank0_values[4] == "6000");
  CHECK(rank0_values[5] == "4200");
  CHECK(rank0_values[7] == "4200");
  CHECK(rank0_values[9] == "0");
  CHECK(rank0_values[10] == "70.000");
  CHECK(rank0_values[11] == "0.000");
  CHECK(rank0_values[12] == "100.000");
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
  REQUIRE(values.size() == 12);
  CHECK(values[2] == "1");
  CHECK(values[3] == "0x82220000");
  CHECK(values[7] == "70");
  CHECK(values[8] == "0");
  CHECK(values[9] == "70");
  CHECK(values[10] == "0");
  CHECK(values[11] == "0");
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
  CHECK(row.find(",1,0x82220000,\"sub,quo\"\"te\",1,100,70,0,70,0,0") !=
        std::string::npos);
}
