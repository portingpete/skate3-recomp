#include <catch2/catch_test_macros.hpp>

#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::kernel::xboxkrnl {
u32 XexUnloadImageAndExitThread_entry(mapped_void hmodule, u32 exit_code);
}  // namespace rex::kernel::xboxkrnl

namespace {
constexpr u32 kStatusInvalidHandle = 0xC0000008u;
}  // namespace

TEST_CASE("XexUnloadImageAndExitThread is guarded outside guest threads",
          "[kernel][xboxkrnl][modules]") {
  CHECK(rex::kernel::xboxkrnl::XexUnloadImageAndExitThread_entry(
            mapped_void(nullptr), 0x1234) == kStatusInvalidHandle);
}
