#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::kernel::xboxkrnl {
u32 MmLockAndMapSegmentArray_entry(u32 flags, mapped_void segment_array, u32 segment_count,
                                   u32 map_flags);
void MmUnlockAndUnmapSegmentArray_entry(mapped_void mapped_address);
}  // namespace rex::kernel::xboxkrnl

TEST_CASE("Mm segment-array map and unmap are deterministic no-ops",
          "[kernel][xboxkrnl][memory]") {
  CHECK(rex::kernel::xboxkrnl::MmLockAndMapSegmentArray_entry(
            0, mapped_void(nullptr), 1, 0) == 0);

  std::array<uint8_t, 0x20> segments{};
  segments.fill(0xAB);
  auto segment_array = mapped_void(segments.data(), 0x40001000);

  CHECK(rex::kernel::xboxkrnl::MmLockAndMapSegmentArray_entry(0, segment_array, 0, 0) == 0);
  CHECK(rex::kernel::xboxkrnl::MmLockAndMapSegmentArray_entry(0x11, segment_array, 2, 0x22) ==
        0x40001000);
  CHECK(std::all_of(segments.begin(), segments.end(),
                    [](uint8_t value) { return value == 0xAB; }));

  rex::kernel::xboxkrnl::MmUnlockAndUnmapSegmentArray_entry(mapped_void(nullptr));
  rex::kernel::xboxkrnl::MmUnlockAndUnmapSegmentArray_entry(segment_array);
  CHECK(std::all_of(segments.begin(), segments.end(),
                    [](uint8_t value) { return value == 0xAB; }));
}
