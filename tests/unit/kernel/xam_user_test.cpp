#include <catch2/catch_test_macros.hpp>

#include <array>

#include <rex/system/xtypes.h>
#include <rex/system/xam/user_profile.h>
#include <rex/types.h>

namespace rex::kernel::xam {
u32 XamGetLocale_entry();
u32 XamGetOnlineCountryFromLocale_entry(u32 id);
u32 XamUserCreateStatsEnumerator_entry(u32 title_id, u32 user_index, u32 xuid_low, u32 flags,
                                       u32 stat_count, mapped_void stat_specs,
                                       mapped_u32 buffer_size_ptr, mapped_u32 handle_ptr);
u32 XamUserGetMembershipTierFromXUID_entry(u64 xuid);
u32 XamUserGetOnlineCountryFromXUID_entry(u64 xuid);
u32 XamParseGamerTileKey_entry(mapped_u32 key_ptr, mapped_u32 out1_ptr, mapped_u32 out2_ptr,
                               mapped_u32 out3_ptr);
u32 XamReadTileToTexture_entry(u32 unknown, u32 title_id, u64 tile_id, u32 user_index,
                               mapped_void buffer_ptr, u32 stride, u32 height, u32 overlapped_ptr);
}  // namespace rex::kernel::xam

TEST_CASE("XUID user queries return deterministic offline profile data", "[kernel][xam_user]") {
  constexpr u64 kXuid = 0x0009000000012345ull;

  CHECK(rex::kernel::xam::XamUserGetMembershipTierFromXUID_entry(0) == 0);
  CHECK(rex::kernel::xam::XamUserGetMembershipTierFromXUID_entry(kXuid) == 6);

  const u32 expected_country =
      rex::kernel::xam::XamGetOnlineCountryFromLocale_entry(rex::kernel::xam::XamGetLocale_entry());
  CHECK(rex::kernel::xam::XamUserGetOnlineCountryFromXUID_entry(0) == 0);
  CHECK(rex::kernel::xam::XamUserGetOnlineCountryFromXUID_entry(kXuid) == expected_country);
}

TEST_CASE("Stats enumerator fails without fabricating handles", "[kernel][xam_user]") {
  constexpr u32 kFunctionFailed = 0x65B;

  rex::be_u32 buffer_size = 0xAAAAAAAAu;
  rex::be_u32 handle = 0xBBBBBBBBu;

  CHECK(rex::kernel::xam::XamUserCreateStatsEnumerator_entry(
            0x58410A71, 0, 0, 0, 0, mapped_void(nullptr), mapped_u32(&buffer_size, 0x40001000),
            mapped_u32(&handle, 0x40001004)) == kFunctionFailed);
  CHECK(static_cast<u32>(buffer_size) == 0);
  CHECK(static_cast<u32>(handle) == 0);

  CHECK(rex::kernel::xam::XamUserCreateStatsEnumerator_entry(
            0x58410A71, 0, 0, 0, 0, mapped_void(nullptr), mapped_u32(nullptr),
            mapped_u32(nullptr)) == kFunctionFailed);
}

TEST_CASE("Gamer tile key parser rejects invalid setting data without clobbering outputs",
          "[kernel][xam_user]") {
  constexpr u32 kInvalidParameter = 0x57;

  rex::be_u32 out1 = 0xAAAAAAAAu;
  rex::be_u32 out2 = 0xBBBBBBBBu;
  rex::be_u32 out3 = 0xCCCCCCCCu;

  CHECK(rex::kernel::xam::XamParseGamerTileKey_entry(
            mapped_u32(nullptr), mapped_u32(&out1, 0x40002000),
            mapped_u32(&out2, 0x40002004), mapped_u32(&out3, 0x40002008)) ==
        kInvalidParameter);
  CHECK(static_cast<u32>(out1) == 0xAAAAAAAAu);
  CHECK(static_cast<u32>(out2) == 0xBBBBBBBBu);
  CHECK(static_cast<u32>(out3) == 0xCCCCCCCCu);

  rex::system::xam::X_USER_PROFILE_SETTING_DATA int_data{};
  int_data.type = static_cast<uint8_t>(rex::system::xam::UserProfile::Setting::Type::INT32);
  int_data.s32 = 0x12345678;

  CHECK(rex::kernel::xam::XamParseGamerTileKey_entry(
            mapped_u32(reinterpret_cast<rex::be_u32*>(&int_data), 0x40001000),
            mapped_u32(&out1, 0x40002000), mapped_u32(&out2, 0x40002004),
            mapped_u32(&out3, 0x40002008)) == kInvalidParameter);
  CHECK(static_cast<u32>(out1) == 0xAAAAAAAAu);
  CHECK(static_cast<u32>(out2) == 0xBBBBBBBBu);
  CHECK(static_cast<u32>(out3) == 0xCCCCCCCCu);
}

TEST_CASE("Gamer tile texture fallback validates inputs and fills white pixels",
          "[kernel][xam_user]") {
  constexpr u32 kInvalidParameter = 0x57;
  std::array<u8, 12> buffer{};

  CHECK(rex::kernel::xam::XamReadTileToTexture_entry(
            9, 0x58410A71, 0, 0, mapped_void(buffer.data(), 0x40003000), 4, 3, 0) ==
        kInvalidParameter);

  CHECK(rex::kernel::xam::XamReadTileToTexture_entry(9, 0x58410A71, 1, 0, mapped_void(nullptr), 0,
                                                     0, 0) == kInvalidParameter);

  CHECK(rex::kernel::xam::XamReadTileToTexture_entry(
            9, 0x58410A71, 1, 0, mapped_void(buffer.data(), 0x40003000), 4, 3, 0) ==
        0);
  for (u8 byte : buffer) {
    CHECK(byte == 0xFFu);
  }
}
