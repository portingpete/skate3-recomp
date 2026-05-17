#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace {
constexpr u32 kStatusDeviceNotConnected = 0xC000009Du;
}

namespace rex::kernel::xboxkrnl {
u32 MicDeviceRequest_entry(mapped_void request_ptr);
u32 RmcDeviceRequest_entry(mapped_void request_ptr);
}  // namespace rex::kernel::xboxkrnl

TEST_CASE("Unsupported media device requests report device not connected",
          "[kernel][xboxkrnl][device_request]") {
  CHECK(rex::kernel::xboxkrnl::MicDeviceRequest_entry(mapped_void(nullptr)) ==
        kStatusDeviceNotConnected);
  CHECK(rex::kernel::xboxkrnl::RmcDeviceRequest_entry(mapped_void(nullptr)) ==
        kStatusDeviceNotConnected);

  CHECK(rex::kernel::xboxkrnl::MicDeviceRequest_entry(
            mapped_void(reinterpret_cast<void*>(uintptr_t{0x1}), 0x40001000)) ==
        kStatusDeviceNotConnected);
  CHECK(rex::kernel::xboxkrnl::RmcDeviceRequest_entry(
            mapped_void(reinterpret_cast<void*>(uintptr_t{0x1}), 0x40002000)) ==
        kStatusDeviceNotConnected);
}
