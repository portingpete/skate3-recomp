#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::kernel::xam {
u32 XamVoiceSubmitPacket_entry(mapped_void voice_ptr, u32 local_user_index,
                               mapped_void packet_ptr);
}  // namespace rex::kernel::xam

TEST_CASE("XamVoiceSubmitPacket is an explicit offline no-op success", "[kernel][xam_voice]") {
  CHECK(rex::kernel::xam::XamVoiceSubmitPacket_entry(mapped_void(nullptr), 0,
                                                     mapped_void(nullptr)) == 0);
  CHECK(rex::kernel::xam::XamVoiceSubmitPacket_entry(
            mapped_void(reinterpret_cast<void*>(uintptr_t{0x1234}), 0x1234), 1,
            mapped_void(reinterpret_cast<void*>(uintptr_t{0x5678}), 0x5678)) == 0);
}
