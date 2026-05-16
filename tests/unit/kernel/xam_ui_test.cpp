#include <catch2/catch_test_macros.hpp>

#include <rex/types.h>

namespace rex::kernel::xam {
u32 XamShowAchievementsUI_entry(u32 user_index, u32 title_id);
u32 XamShowFriendsUI_entry(u32 user_index);
u32 XamShowGamerCardUIForXUID_entry(u32 user_index, u64 xuid);
u32 XamShowFriendRequestUI_entry();
u32 XamShowGameInviteUI_entry();
u32 XamShowMarketplaceUI_entry();
u32 XamShowMarketplaceUIEx_entry();
u32 XamShowMarketplaceDownloadItemsUI_entry();
u32 XamShowMessageComposeUI_entry();
u32 XamShowMessagesUI_entry();
u32 XamShowMessagesUIEx_entry();
u32 XamShowPlayerReviewUI_entry();
u32 XamShowPlayersUI_entry();
u32 XamShowQuickChatUI_entry();
u32 XamShowQuickChatUIp_entry();
u32 XamShowVoiceMailUI_entry();
}  // namespace rex::kernel::xam

TEST_CASE("Unavailable social guide UI is a no-op success", "[kernel][xam_ui]") {
  using namespace rex::kernel::xam;
  constexpr u32 kSuccess = 0;

  CHECK(XamShowAchievementsUI_entry(0, 0) == kSuccess);
  CHECK(XamShowFriendsUI_entry(0) == kSuccess);
  CHECK(XamShowGamerCardUIForXUID_entry(0, 0x1234) == kSuccess);
  CHECK(XamShowFriendRequestUI_entry() == kSuccess);
  CHECK(XamShowGameInviteUI_entry() == kSuccess);
  CHECK(XamShowMarketplaceUI_entry() == kSuccess);
  CHECK(XamShowMarketplaceUIEx_entry() == kSuccess);
  CHECK(XamShowMarketplaceDownloadItemsUI_entry() == kSuccess);
  CHECK(XamShowMessageComposeUI_entry() == kSuccess);
  CHECK(XamShowMessagesUI_entry() == kSuccess);
  CHECK(XamShowMessagesUIEx_entry() == kSuccess);
  CHECK(XamShowPlayerReviewUI_entry() == kSuccess);
  CHECK(XamShowPlayersUI_entry() == kSuccess);
  CHECK(XamShowQuickChatUI_entry() == kSuccess);
  CHECK(XamShowQuickChatUIp_entry() == kSuccess);
  CHECK(XamShowVoiceMailUI_entry() == kSuccess);
}
