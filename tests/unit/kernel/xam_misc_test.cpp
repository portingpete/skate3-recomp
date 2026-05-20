#include <catch2/catch_test_macros.hpp>

#include <rex/chrono/clock.h>
#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::kernel::xboxkrnl {
u32 KeQueryPerformanceFrequency_entry();
}  // namespace rex::kernel::xboxkrnl

namespace rex::kernel::xam {
u32 GetTickCount_entry();
void GetSystemTimeAsFileTime_entry(mapped_u64 time_ptr);
u32 QueryPerformanceCounter_entry(mapped_u64 counter_ptr);
u32 QueryPerformanceFrequency_entry(mapped_u64 frequency_ptr);
u32 Refresh_entry(mapped_void refresh_context, u32 refresh_flags, u32 refresh_arg);
u32 XamBackgroundDownloadItemGetStatus_entry(mapped_void content_data, mapped_void item_data,
                                             u32 flags, u32 item_count, mapped_u32 state_ptr,
                                             mapped_u32 progress_ptr,
                                             mapped_u32 result_ptr);
u32 XamBackgroundDownloadItemGetHistoryStatus_entry(mapped_void content_data,
                                                    mapped_void item_data, u32 flags);
}  // namespace rex::kernel::xam

TEST_CASE("XAM performance queries mirror the guest clock", "[kernel][xam]") {
  const u32 expected_frequency = rex::kernel::xboxkrnl::KeQueryPerformanceFrequency_entry();
  rex::be_u64 frequency = 0;
  CHECK(rex::kernel::xam::QueryPerformanceFrequency_entry(
            mapped_u64(&frequency, 0x40001000)) == 1);
  CHECK(static_cast<uint64_t>(frequency) == expected_frequency);
  CHECK(static_cast<uint64_t>(frequency) > 0);
  CHECK(rex::kernel::xam::QueryPerformanceFrequency_entry(mapped_u64(nullptr)) == 0);

  rex::be_u64 counter1 = 0;
  rex::be_u64 counter2 = 0;
  CHECK(rex::kernel::xam::QueryPerformanceCounter_entry(mapped_u64(&counter1, 0x40001008)) ==
        1);
  CHECK(rex::kernel::xam::QueryPerformanceCounter_entry(mapped_u64(&counter2, 0x40001010)) ==
        1);
  CHECK(static_cast<uint64_t>(counter2) >= static_cast<uint64_t>(counter1));
  CHECK(rex::kernel::xam::QueryPerformanceCounter_entry(mapped_u64(nullptr)) == 0);
}

TEST_CASE("XAM wall-clock queries mirror the guest clock", "[kernel][xam]") {
  constexpr u32 kClockSkewToleranceMs = 1000;
  constexpr u64 kClockSkewToleranceFileTime = u64(kClockSkewToleranceMs) * 10000;

  const u32 tick_before = rex::chrono::Clock::QueryGuestUptimeMillis();
  const u32 tick = rex::kernel::xam::GetTickCount_entry();
  const u32 tick_after = rex::chrono::Clock::QueryGuestUptimeMillis();
  CHECK(tick + kClockSkewToleranceMs >= tick_before);
  CHECK(tick <= tick_after + kClockSkewToleranceMs);

  rex::be_u64 file_time = 0;
  const u64 file_time_before = rex::chrono::Clock::QueryGuestSystemTime();
  rex::kernel::xam::GetSystemTimeAsFileTime_entry(mapped_u64(&file_time, 0x40002000));
  const u64 file_time_after = rex::chrono::Clock::QueryGuestSystemTime();
  CHECK(static_cast<uint64_t>(file_time) + kClockSkewToleranceFileTime >= file_time_before);
  CHECK(static_cast<uint64_t>(file_time) <= file_time_after + kClockSkewToleranceFileTime);

  rex::kernel::xam::GetSystemTimeAsFileTime_entry(mapped_u64(nullptr));
}

TEST_CASE("Refresh is a deterministic offline no-op success", "[kernel][xam]") {
  constexpr u32 kSuccess = 0;

  CHECK(rex::kernel::xam::Refresh_entry(mapped_void(nullptr), 0, 0) == kSuccess);
  CHECK(rex::kernel::xam::Refresh_entry(mapped_void(nullptr, 0x40001000), 0x1234,
                                        0x80000000) == kSuccess);
}

TEST_CASE("Background download item status reports no active offline item", "[kernel][xam]") {
  constexpr u32 kSuccess = 0;
  constexpr u32 kNotFound = 1168;

  rex::be_u32 state = 0xFFFFFFFFu;
  rex::be_u32 progress = 0xFFFFFFFFu;
  rex::be_u32 result = 0xFFFFFFFFu;

  CHECK(rex::kernel::xam::XamBackgroundDownloadItemGetStatus_entry(
            mapped_void(nullptr, 0x40001000), mapped_void(nullptr, 0x40002000), 0, 1,
            mapped_u32(&state, 0x40003000), mapped_u32(&progress, 0x40003004),
            mapped_u32(&result, 0x40003008)) == kSuccess);
  CHECK(static_cast<u32>(state) == 0);
  CHECK(static_cast<u32>(progress) == 0);
  CHECK(static_cast<u32>(result) == 0);

  CHECK(rex::kernel::xam::XamBackgroundDownloadItemGetStatus_entry(
            mapped_void(nullptr, 0x40001000), mapped_void(nullptr, 0x40002000), 0, 1,
            mapped_u32(nullptr), mapped_u32(nullptr), mapped_u32(nullptr)) == kSuccess);

  CHECK(rex::kernel::xam::XamBackgroundDownloadItemGetHistoryStatus_entry(
            mapped_void(nullptr, 0x40001000), mapped_void(nullptr, 0x40002000), 1) ==
        kNotFound);
}
