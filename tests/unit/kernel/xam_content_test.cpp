#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

#include <rex/kernel/xam/private.h>
#include <rex/system/xam/content_device.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xtypes.h>
#include <rex/types.h>

using X_RESULT = rex::X_RESULT;

namespace rex::kernel::xam {
std::string BuildXamContentResolvePath(const rex::system::xam::XCONTENT_DATA& content_data,
                                       uint64_t xuid, uint32_t title_id);
u32 XamContentResolve_entry(u32 user_index, mapped_void content_data_ptr, mapped_void buffer_ptr,
                            u32 buffer_size, u32 unk1, u32 unk2, u32 unk3);
u32 XamContentInstall_entry();
u32 XamContentInstallInternal_entry();
u32 XamContentLaunchImage_entry();
u32 XamContentLaunchImageFromFileInternal_entry();
u32 XamContentLaunchImageInternal_entry();
u32 XamContentLaunchImageInternalEx_entry();
}  // namespace rex::kernel::xam

TEST_CASE("Content resolve builds paths for disc and profile content", "[kernel][xam_content]") {
  using rex::system::XContentType;
  using rex::system::xam::DummyDeviceId;
  using rex::system::xam::XCONTENT_DATA;

  XCONTENT_DATA content_data{};
  content_data.set_file_name("SAVEGAME0001");

  content_data.device_id = static_cast<uint32_t>(DummyDeviceId::ODD);
  content_data.content_type = XContentType::kSavedGame;
  CHECK(rex::kernel::xam::BuildXamContentResolvePath(content_data, 0x1122334455667788ull,
                                                     0x584108A9) ==
        "game:\\Content\\0000000000000000\\584108A9\\00000001\\SAVEGAME0001");

  content_data.device_id = static_cast<uint32_t>(DummyDeviceId::HDD);
  content_data.content_type = XContentType::kSavedGame;
  CHECK(rex::kernel::xam::BuildXamContentResolvePath(content_data, 0x1122334455667788ull,
                                                     0x584108A9) ==
        "content:\\1122334455667788\\584108A9\\00000001\\SAVEGAME0001");

  content_data.content_type = XContentType::kMarketplaceContent;
  content_data.set_file_name("DLC0000000000001");
  CHECK(rex::kernel::xam::BuildXamContentResolvePath(content_data, 0x1122334455667788ull,
                                                     0x584108A9) ==
        "content:\\0000000000000000\\584108A9\\00000002\\DLC0000000000001");
}

TEST_CASE("Content resolve validates arguments before runtime state", "[kernel][xam_content]") {
  using rex::system::XContentType;
  using rex::system::xam::XCONTENT_DATA;

  XCONTENT_DATA content_data{};
  content_data.set_file_name("SAVEGAME0001");
  content_data.device_id = 0xDEADBEEFu;
  content_data.content_type = XContentType::kSavedGame;
  std::array<char, 260> buffer{};

  CHECK(rex::kernel::xam::XamContentResolve_entry(0, mapped_void(nullptr),
                                                  mapped_void(buffer.data(), 0x40001000),
                                                  static_cast<u32>(buffer.size()), 0, 0, 0) ==
        X_ERROR_INVALID_PARAMETER);
  CHECK(rex::kernel::xam::XamContentResolve_entry(
            0, mapped_void(&content_data, 0x40002000), mapped_void(nullptr),
            static_cast<u32>(buffer.size()), 0, 0, 0) == X_ERROR_INVALID_PARAMETER);
  CHECK(rex::kernel::xam::XamContentResolve_entry(0, mapped_void(&content_data, 0x40002000),
                                                  mapped_void(buffer.data(), 0x40001000), 0, 0, 0,
                                                  0) == X_ERROR_INSUFFICIENT_BUFFER);
  CHECK(rex::kernel::xam::XamContentResolve_entry(
            0, mapped_void(&content_data, 0x40002000), mapped_void(buffer.data(), 0x40001000),
            static_cast<u32>(buffer.size()), 0, 0, 0) == X_ERROR_DEVICE_NOT_CONNECTED);
}

TEST_CASE("Unsupported content install and launch helpers fail deterministically",
          "[kernel][xam_content]") {
  constexpr u32 kFunctionFailed = 0x65B;

  CHECK(rex::kernel::xam::XamContentInstall_entry() == kFunctionFailed);
  CHECK(rex::kernel::xam::XamContentInstallInternal_entry() == kFunctionFailed);
  CHECK(rex::kernel::xam::XamContentLaunchImage_entry() == kFunctionFailed);
  CHECK(rex::kernel::xam::XamContentLaunchImageFromFileInternal_entry() == kFunctionFailed);
  CHECK(rex::kernel::xam::XamContentLaunchImageInternal_entry() == kFunctionFailed);
  CHECK(rex::kernel::xam::XamContentLaunchImageInternalEx_entry() == kFunctionFailed);
}
