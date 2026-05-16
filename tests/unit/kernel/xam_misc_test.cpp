#include <catch2/catch_test_macros.hpp>

#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::kernel::xam {
u32 XamBackgroundDownloadItemGetStatus_entry(mapped_void content_data, mapped_void item_data,
                                             u32 flags, u32 item_count, mapped_u32 state_ptr,
                                             mapped_u32 progress_ptr,
                                             mapped_u32 result_ptr);
u32 XamBackgroundDownloadItemGetHistoryStatus_entry(mapped_void content_data,
                                                    mapped_void item_data, u32 flags);
}  // namespace rex::kernel::xam

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
