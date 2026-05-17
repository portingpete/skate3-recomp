#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::kernel::xboxkrnl {
u32 ExGetXConfigSetting_entry(u16 category, u16 setting, mapped_void buffer_ptr, u16 buffer_size,
                              mapped_u16 required_size_ptr);
}  // namespace rex::kernel::xboxkrnl

namespace {
constexpr u16 kUserCategory = 0x0003;
constexpr u16 kConsoleCategory = 0x0007;
constexpr u16 kPcFlags = 0x000F;
constexpr u16 kPcGame = 0x0016;
constexpr u16 kPcGameRating = 0x0019;
constexpr u16 kConsoleCameraSettings = 0x0004;
constexpr u32 kStatusSuccess = 0x00000000;
constexpr u32 kStatusBufferTooSmall = 0xC0000023;
}  // namespace

TEST_CASE("XConfig reports PC compatibility settings", "[kernel][xboxkrnl][xconfig]") {
  rex::be_u16 required_size = 0xBEEFu;
  std::array<uint8_t, 4> buffer{};

  buffer.fill(0xCD);
  CHECK(rex::kernel::xboxkrnl::ExGetXConfigSetting_entry(
            kUserCategory, kPcFlags, mapped_void(buffer.data(), 0x40001000), 1,
            mapped_u16(&required_size, 0x40002000)) == kStatusSuccess);
  CHECK(required_size == 1);
  CHECK(buffer[0] == 0x03);
  CHECK(buffer[1] == 0xCD);

  buffer.fill(0xCD);
  required_size = 0xBEEFu;
  CHECK(rex::kernel::xboxkrnl::ExGetXConfigSetting_entry(
            kUserCategory, kPcGame, mapped_void(buffer.data(), 0x40001000), 4,
            mapped_u16(&required_size, 0x40002000)) == kStatusSuccess);
  CHECK(required_size == 4);
  CHECK(buffer == std::array<uint8_t, 4>{0x00, 0x00, 0x00, 0xFF});

  buffer.fill(0xCD);
  required_size = 0xBEEFu;
  CHECK(rex::kernel::xboxkrnl::ExGetXConfigSetting_entry(
            kUserCategory, kPcGameRating, mapped_void(buffer.data(), 0x40001000), 4,
            mapped_u16(&required_size, 0x40002000)) == kStatusSuccess);
  CHECK(required_size == 4);
  CHECK(buffer == std::array<uint8_t, 4>{0x00, 0x00, 0x00, 0x00});
}

TEST_CASE("XConfig reports console camera settings", "[kernel][xboxkrnl][xconfig]") {
  rex::be_u16 required_size = 0xBEEFu;
  std::array<uint8_t, 4> buffer{};
  buffer.fill(0xCD);

  CHECK(rex::kernel::xboxkrnl::ExGetXConfigSetting_entry(
            kConsoleCategory, kConsoleCameraSettings, mapped_void(buffer.data(), 0x40001000), 4,
            mapped_u16(&required_size, 0x40002000)) == kStatusSuccess);
  CHECK(required_size == 4);
  CHECK(buffer == std::array<uint8_t, 4>{0x00, 0x00, 0x00, 0x01});
}

TEST_CASE("XConfig PC game rating requires four bytes", "[kernel][xboxkrnl][xconfig]") {
  rex::be_u16 required_size = 0xBEEFu;
  std::array<uint8_t, 4> buffer{};
  buffer.fill(0xCD);

  CHECK(rex::kernel::xboxkrnl::ExGetXConfigSetting_entry(
            kUserCategory, kPcGameRating, mapped_void(buffer.data(), 0x40001000), 1,
            mapped_u16(&required_size, 0x40002000)) == kStatusBufferTooSmall);
  CHECK(buffer == std::array<uint8_t, 4>{0xCD, 0xCD, 0xCD, 0xCD});
}
