#include <catch2/catch_test_macros.hpp>

#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::kernel::xam {
u32 XamGetLocale_entry();
u32 XamGetOnlineCountryFromLocale_entry(u32 id);
u32 XamUserCreateStatsEnumerator_entry(u32 title_id, u32 user_index, u32 xuid_low, u32 flags,
                                       u32 stat_count, mapped_void stat_specs,
                                       mapped_u32 buffer_size_ptr, mapped_u32 handle_ptr);
u32 XamUserGetMembershipTierFromXUID_entry(u64 xuid);
u32 XamUserGetOnlineCountryFromXUID_entry(u64 xuid);
}  // namespace rex::kernel::xam

TEST_CASE("XUID user queries return deterministic offline profile data", "[kernel][xam_user]") {
  constexpr u64 kXuid = 0x0009000000012345ull;

  CHECK(rex::kernel::xam::XamUserGetMembershipTierFromXUID_entry(0) == 0);
  CHECK(rex::kernel::xam::XamUserGetMembershipTierFromXUID_entry(kXuid) == 6);

  const u32 expected_country = rex::kernel::xam::XamGetOnlineCountryFromLocale_entry(
      rex::kernel::xam::XamGetLocale_entry());
  CHECK(rex::kernel::xam::XamUserGetOnlineCountryFromXUID_entry(0) == 0);
  CHECK(rex::kernel::xam::XamUserGetOnlineCountryFromXUID_entry(kXuid) == expected_country);
}

TEST_CASE("Stats enumerator fails without fabricating handles", "[kernel][xam_user]") {
  constexpr u32 kFunctionFailed = 0x65B;

  rex::be_u32 buffer_size = 0xAAAAAAAAu;
  rex::be_u32 handle = 0xBBBBBBBBu;

  CHECK(rex::kernel::xam::XamUserCreateStatsEnumerator_entry(
            0x58410A71, 0, 0, 0, 0, mapped_void(nullptr),
            mapped_u32(&buffer_size, 0x40001000), mapped_u32(&handle, 0x40001004)) ==
        kFunctionFailed);
  CHECK(static_cast<u32>(buffer_size) == 0);
  CHECK(static_cast<u32>(handle) == 0);

  CHECK(rex::kernel::xam::XamUserCreateStatsEnumerator_entry(
            0x58410A71, 0, 0, 0, 0, mapped_void(nullptr), mapped_u32(nullptr),
            mapped_u32(nullptr)) == kFunctionFailed);
}
