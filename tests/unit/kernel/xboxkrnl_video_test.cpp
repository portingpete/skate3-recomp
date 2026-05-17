#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::kernel::xboxkrnl {
u32 VdQuerySystemCommandBuffer_entry(u32 selector);
void VdSetSystemCommandBuffer_entry(mapped_void buffer_ptr);
}  // namespace rex::kernel::xboxkrnl

TEST_CASE("Vd system command-buffer query and set are deterministic no-ops",
          "[kernel][xboxkrnl][video]") {
  CHECK(rex::kernel::xboxkrnl::VdQuerySystemCommandBuffer_entry(0) == 0);
  CHECK(rex::kernel::xboxkrnl::VdQuerySystemCommandBuffer_entry(0xFFFFFFFFu) == 0);

  std::array<uint8_t, 0x20> buffer{};
  buffer.fill(0xCD);

  rex::kernel::xboxkrnl::VdSetSystemCommandBuffer_entry(mapped_void(nullptr));
  rex::kernel::xboxkrnl::VdSetSystemCommandBuffer_entry(mapped_void(buffer.data(), 0x40001000));

  CHECK(std::all_of(buffer.begin(), buffer.end(),
                    [](uint8_t value) { return value == 0xCD; }));
}
