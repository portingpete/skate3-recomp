#include <catch2/catch_test_macros.hpp>

#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace {
constexpr u32 kErrorNotReady = 0x15u;
}

namespace rex::kernel::xboxkrnl {
u32 XUsbcamCreate_entry(u32 buffer, u32 buffer_size, mapped_void unk3_ptr);
u32 XUsbcamDestroy_entry(u32 handle);
u32 XUsbcamGetState_entry();
u32 XUsbcamSetCaptureMode_entry(u32 handle, u32 mode, u32 format, u32 width, u32 height,
                                u32 pitch, mapped_void config_ptr, mapped_void overlapped_ptr,
                                mapped_void token_ptr);
u32 XUsbcamSetConfig_entry(u32 handle, u32 category, u32 control, mapped_void value_ptr,
                           u32 value_size, mapped_void defaults_ptr, mapped_void overlapped_ptr);
u32 XUsbcamSetView_entry(u32 handle, u32 view, u32 flags, u32 buffer, mapped_void token_ptr);
u32 XUsbcamReadFrame_entry(u32 handle, u32 frame, u32 buffer, u32 buffer_size,
                           mapped_void info_ptr, mapped_void overlapped_ptr);
u32 XUsbcamGetConfig_entry(u32 handle, u32 category, u32 control, mapped_void value_ptr,
                           mapped_u32 value_size_ptr);
u32 XUsbcamSnapshot_entry(u32 handle, mapped_void buffer, mapped_u32 buffer_size_ptr);
u32 XUsbcamGetView_entry(u32 handle, mapped_void view_ptr);
u32 XUsbcamReset_entry(u32 handle);
}  // namespace rex::kernel::xboxkrnl

TEST_CASE("USB camera exports report a stable no-camera state", "[kernel][xboxkrnl][usbcam]") {
  CHECK(rex::kernel::xboxkrnl::XUsbcamCreate_entry(0, 0x4B000, mapped_void(nullptr)) == 0);
  CHECK(rex::kernel::xboxkrnl::XUsbcamGetState_entry() == 0);
  CHECK(rex::kernel::xboxkrnl::XUsbcamDestroy_entry(1) == 0);
  CHECK(rex::kernel::xboxkrnl::XUsbcamReset_entry(1) == 0);

  CHECK(rex::kernel::xboxkrnl::XUsbcamSetCaptureMode_entry(
            1, 4, 0, 640, 480, 0, mapped_void(nullptr), mapped_void(nullptr),
            mapped_void(nullptr)) == kErrorNotReady);
  CHECK(rex::kernel::xboxkrnl::XUsbcamSetConfig_entry(
            1, 0, 0, mapped_void(nullptr), 0, mapped_void(nullptr), mapped_void(nullptr)) ==
        kErrorNotReady);
  CHECK(rex::kernel::xboxkrnl::XUsbcamSetView_entry(1, 0, 0, 0, mapped_void(nullptr)) ==
        kErrorNotReady);
  CHECK(rex::kernel::xboxkrnl::XUsbcamReadFrame_entry(
            1, 0, 0, 0, mapped_void(nullptr), mapped_void(nullptr)) == kErrorNotReady);
  CHECK(rex::kernel::xboxkrnl::XUsbcamGetConfig_entry(
            1, 0, 0, mapped_void(nullptr), mapped_u32(nullptr)) == kErrorNotReady);
  CHECK(rex::kernel::xboxkrnl::XUsbcamSnapshot_entry(1, mapped_void(nullptr),
                                                    mapped_u32(nullptr)) == kErrorNotReady);
  CHECK(rex::kernel::xboxkrnl::XUsbcamGetView_entry(1, mapped_void(nullptr)) == kErrorNotReady);
}
