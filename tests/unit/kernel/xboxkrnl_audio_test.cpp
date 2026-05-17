#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::kernel::xboxkrnl {
u32 XAudioQueryDriverPerformance_entry(mapped_void driver_ptr, mapped_void performance_ptr);
}  // namespace rex::kernel::xboxkrnl

TEST_CASE("XAudioQueryDriverPerformance reports empty performance data",
          "[kernel][xboxkrnl][audio]") {
  auto driver_ptr = mapped_void(reinterpret_cast<void*>(uintptr_t{0x1}), 0x41550001);

  CHECK(rex::kernel::xboxkrnl::XAudioQueryDriverPerformance_entry(driver_ptr,
                                                                  mapped_void(nullptr)) == 0);

  std::array<uint8_t, 0x40> performance{};
  performance.fill(0xAB);

  CHECK(rex::kernel::xboxkrnl::XAudioQueryDriverPerformance_entry(
            driver_ptr, mapped_void(performance.data(), 0x40001000)) == 0);
  CHECK(std::all_of(performance.begin(), performance.begin() + 0x30,
                    [](uint8_t value) { return value == 0; }));
  CHECK(std::all_of(performance.begin() + 0x30, performance.end(),
                    [](uint8_t value) { return value == 0xAB; }));
}
