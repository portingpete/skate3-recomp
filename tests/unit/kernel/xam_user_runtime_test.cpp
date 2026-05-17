#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <vector>

#include <rex/kernel/init.h>
#include <rex/memory.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>
#include <rex/system/xio.h>
#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::kernel::xam {
u32 XamUserReadProfileSettings_entry(u32 title_id, u32 user_index, u32 xuid_count, mapped_u64 xuids,
                                     u32 setting_count, mapped_u32 setting_ids,
                                     mapped_u32 buffer_size_ptr, mapped_void buffer_ptr,
                                     ppc_ptr_t<rex::system::XAM_OVERLAPPED> overlapped);
u32 XamUserWriteProfileSettings_entry(u32 title_id, u32 user_index, u32 setting_count,
                                      ppc_ptr_t<rex::system::xam::X_USER_PROFILE_SETTING> settings,
                                      ppc_ptr_t<rex::system::XAM_OVERLAPPED> overlapped);
}  // namespace rex::kernel::xam

namespace {

struct X_USER_READ_PROFILE_SETTINGS_TEST {
  rex::be<uint32_t> setting_count;
  rex::be<uint32_t> settings_ptr;
};
static_assert(sizeof(X_USER_READ_PROFILE_SETTINGS_TEST) == 8);

}  // namespace

TEST_CASE("Profile setting writes persist scalar values for later reads",
          "[runtime][kernel][xam_user]") {
  constexpr u32 kSuccess = 0;
  constexpr u32 kSettingId = 0x10040040;  // XPROFILE_ENABLE_TUTORIALS.
  constexpr i32 kSettingValue = 0x12345678;

  rex::Runtime runtime({}, {}, {}, {});
  rex::RuntimeConfig config;
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == 0);

  rex::system::xam::X_USER_PROFILE_SETTING write_setting{};
  write_setting.from = 1;
  write_setting.user_index = 0;
  write_setting.setting_id = kSettingId;
  write_setting.data.type =
      static_cast<uint8_t>(rex::system::xam::UserProfile::Setting::Type::INT32);
  write_setting.data.s32 = kSettingValue;

  CHECK(rex::kernel::xam::XamUserWriteProfileSettings_entry(
            0, 0, 1,
            ppc_ptr_t<rex::system::xam::X_USER_PROFILE_SETTING>(&write_setting, 0x40001000),
            ppc_ptr_t<rex::system::XAM_OVERLAPPED>(nullptr)) == kSuccess);

  auto* memory = runtime.kernel_state()->memory();
  const u32 read_buffer_guest = memory->SystemHeapAlloc(0x100);
  auto* read_buffer = memory->TranslateVirtual<uint8_t*>(read_buffer_guest);
  REQUIRE(read_buffer);
  std::memset(read_buffer, 0xCD, 0x100);

  rex::be_u32 setting_id = kSettingId;
  rex::be_u32 buffer_size = 0x100;

  CHECK(rex::kernel::xam::XamUserReadProfileSettings_entry(
            0, 0, 0, mapped_u64(nullptr), 1, mapped_u32(&setting_id, 0x40002000),
            mapped_u32(&buffer_size, 0x40002004), mapped_void(read_buffer, read_buffer_guest),
            ppc_ptr_t<rex::system::XAM_OVERLAPPED>(nullptr)) == kSuccess);

  auto* header = reinterpret_cast<X_USER_READ_PROFILE_SETTINGS_TEST*>(read_buffer);
  auto* out_setting = reinterpret_cast<rex::system::xam::X_USER_PROFILE_SETTING*>(
      read_buffer + sizeof(X_USER_READ_PROFILE_SETTINGS_TEST));

  CHECK(static_cast<u32>(buffer_size) == 0x100);
  CHECK(static_cast<u32>(header->setting_count) == 1);
  CHECK(static_cast<u32>(header->settings_ptr) ==
        read_buffer_guest + sizeof(X_USER_READ_PROFILE_SETTINGS_TEST));
  CHECK(static_cast<u32>(out_setting->from) == 1);
  CHECK(static_cast<u32>(out_setting->user_index) == 0);
  CHECK(static_cast<u32>(out_setting->setting_id) == kSettingId);
  CHECK(out_setting->data.type ==
        static_cast<uint8_t>(rex::system::xam::UserProfile::Setting::Type::INT32));
  CHECK(static_cast<i32>(out_setting->data.s32) == kSettingValue);
}

TEST_CASE("Profile setting writes persist Unicode string values for later reads",
          "[runtime][kernel][xam_user]") {
  constexpr u32 kSuccess = 0;
  constexpr u32 kSettingId = 0x402C0041;
  const std::u16string kSettingValue = u"tutorials";

  rex::Runtime runtime({}, {}, {}, {});
  rex::RuntimeConfig config;
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == 0);

  auto* memory = runtime.kernel_state()->memory();
  const size_t char_count = kSettingValue.size() + 1;
  const u32 unicode_guest =
      memory->SystemHeapAlloc(static_cast<u32>(char_count * sizeof(char16_t)));
  auto* unicode_buffer = memory->TranslateVirtual<uint16_t*>(unicode_guest);
  REQUIRE(unicode_buffer);
  rex::memory::copy_and_swap(unicode_buffer,
                             reinterpret_cast<const uint16_t*>(kSettingValue.c_str()), char_count);

  rex::system::xam::X_USER_PROFILE_SETTING write_setting{};
  write_setting.from = 1;
  write_setting.user_index = 0;
  write_setting.setting_id = kSettingId;
  write_setting.data.type =
      static_cast<uint8_t>(rex::system::xam::UserProfile::Setting::Type::WSTRING);
  write_setting.data.unicode.size = static_cast<u32>(char_count * sizeof(char16_t));
  write_setting.data.unicode.ptr = unicode_guest;

  CHECK(rex::kernel::xam::XamUserWriteProfileSettings_entry(
            0, 0, 1,
            ppc_ptr_t<rex::system::xam::X_USER_PROFILE_SETTING>(&write_setting, 0x40003000),
            ppc_ptr_t<rex::system::XAM_OVERLAPPED>(nullptr)) == kSuccess);

  const u32 read_buffer_guest = memory->SystemHeapAlloc(0x200);
  auto* read_buffer = memory->TranslateVirtual<uint8_t*>(read_buffer_guest);
  REQUIRE(read_buffer);
  std::memset(read_buffer, 0xCD, 0x200);

  rex::be_u32 setting_id = kSettingId;
  rex::be_u32 buffer_size = 0x200;

  CHECK(rex::kernel::xam::XamUserReadProfileSettings_entry(
            0, 0, 0, mapped_u64(nullptr), 1, mapped_u32(&setting_id, 0x40004000),
            mapped_u32(&buffer_size, 0x40004004), mapped_void(read_buffer, read_buffer_guest),
            ppc_ptr_t<rex::system::XAM_OVERLAPPED>(nullptr)) == kSuccess);

  auto* header = reinterpret_cast<X_USER_READ_PROFILE_SETTINGS_TEST*>(read_buffer);
  auto* out_setting = reinterpret_cast<rex::system::xam::X_USER_PROFILE_SETTING*>(
      read_buffer + sizeof(X_USER_READ_PROFILE_SETTINGS_TEST));

  CHECK(static_cast<u32>(header->setting_count) == 1);
  CHECK(static_cast<u32>(out_setting->from) == 1);
  CHECK(static_cast<u32>(out_setting->setting_id) == kSettingId);
  CHECK(out_setting->data.type ==
        static_cast<uint8_t>(rex::system::xam::UserProfile::Setting::Type::WSTRING));

  const u32 out_unicode_guest = out_setting->data.unicode.ptr;
  const size_t out_char_count = static_cast<u32>(out_setting->data.unicode.size) / sizeof(char16_t);
  auto* out_unicode_buffer = memory->TranslateVirtual<uint16_t*>(out_unicode_guest);
  REQUIRE(out_unicode_buffer);

  std::vector<uint16_t> native_chars(out_char_count);
  rex::memory::copy_and_swap(native_chars.data(), out_unicode_buffer, out_char_count);
  REQUIRE(!native_chars.empty());
  CHECK(native_chars.back() == 0);
  CHECK(std::u16string(reinterpret_cast<const char16_t*>(native_chars.data()),
                       native_chars.size() - 1) == kSettingValue);
}
