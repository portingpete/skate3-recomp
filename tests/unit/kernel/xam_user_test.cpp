#include <catch2/catch_test_macros.hpp>

#include <rex/types.h>

namespace rex::kernel::xam {
u32 XamGetLocale_entry();
u32 XamGetOnlineCountryFromLocale_entry(u32 id);
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
