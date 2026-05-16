#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace {
constexpr u32 kStatusInvalidDeviceRequest = 0xC0000010u;
}  // namespace

namespace rex::kernel::xboxkrnl {
u32 IoDismountVolume_entry(mapped_void device_object);
u32 IoInvalidDeviceRequest_entry(mapped_void device_object, mapped_void irp);
u32 IoCheckShareAccess_entry(u32 desired_access, u32 desired_share_access,
                             mapped_void file_object, mapped_void share_access, u32 update);
void IoSetShareAccess_entry(u32 desired_access, u32 desired_share_access,
                            mapped_void file_object, mapped_void share_access);
void IoRemoveShareAccess_entry(mapped_void file_object, mapped_void share_access);
void IoCompleteRequest_entry(mapped_void irp, u32 priority_boost);
void IoDeleteDevice_entry(mapped_void device_object);
}  // namespace rex::kernel::xboxkrnl

TEST_CASE("IoDismountVolume is a safe no-op success", "[kernel][xboxkrnl][io]") {
  CHECK(rex::kernel::xboxkrnl::IoDismountVolume_entry(mapped_void(nullptr)) == 0);
  CHECK(rex::kernel::xboxkrnl::IoDismountVolume_entry(
            mapped_void(reinterpret_cast<void*>(uintptr_t{0x1}), 0x40001000)) == 0);
}

TEST_CASE("IoInvalidDeviceRequest returns invalid-device status", "[kernel][xboxkrnl][io]") {
  CHECK(rex::kernel::xboxkrnl::IoInvalidDeviceRequest_entry(mapped_void(nullptr),
                                                            mapped_void(nullptr)) ==
        kStatusInvalidDeviceRequest);
  CHECK(rex::kernel::xboxkrnl::IoInvalidDeviceRequest_entry(
            mapped_void(reinterpret_cast<void*>(uintptr_t{0x1}), 0x40001000),
            mapped_void(reinterpret_cast<void*>(uintptr_t{0x2}), 0x40002000)) ==
        kStatusInvalidDeviceRequest);
}

TEST_CASE("File share-access helpers are permissive no-ops", "[kernel][xboxkrnl][io]") {
  std::array<uint8_t, 0x20> share_access{};
  share_access.fill(0xAB);
  auto file_object = mapped_void(reinterpret_cast<void*>(uintptr_t{0x1}), 0x40001000);
  auto share_ptr = mapped_void(share_access.data(), 0x40002000);

  CHECK(rex::kernel::xboxkrnl::IoCheckShareAccess_entry(0x80000000u, 0, file_object,
                                                        share_ptr, 1) == 0);
  CHECK(std::all_of(share_access.begin(), share_access.end(),
                    [](uint8_t value) { return value == 0xAB; }));

  rex::kernel::xboxkrnl::IoSetShareAccess_entry(1, 2, file_object, share_ptr);
  rex::kernel::xboxkrnl::IoRemoveShareAccess_entry(file_object, share_ptr);
  CHECK(std::all_of(share_access.begin(), share_access.end(),
                    [](uint8_t value) { return value == 0xAB; }));
}

TEST_CASE("I/O completion cleanup helpers are safe no-ops", "[kernel][xboxkrnl][io]") {
  std::array<uint8_t, 0x40> irp{};
  std::array<uint8_t, 0x40> device_object{};
  irp.fill(0xCD);
  device_object.fill(0xEF);

  rex::kernel::xboxkrnl::IoCompleteRequest_entry(mapped_void(nullptr), 0);
  rex::kernel::xboxkrnl::IoCompleteRequest_entry(mapped_void(irp.data(), 0x40001000), 1);
  rex::kernel::xboxkrnl::IoDeleteDevice_entry(mapped_void(nullptr));
  rex::kernel::xboxkrnl::IoDeleteDevice_entry(mapped_void(device_object.data(), 0x40002000));

  CHECK(std::all_of(irp.begin(), irp.end(), [](uint8_t value) { return value == 0xCD; }));
  CHECK(std::all_of(device_object.begin(), device_object.end(),
                    [](uint8_t value) { return value == 0xEF; }));
}
