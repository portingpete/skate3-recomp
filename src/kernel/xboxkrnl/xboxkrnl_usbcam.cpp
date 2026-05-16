/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/kernel/xboxkrnl/private.h>
#include <rex/logging.h>
#include <rex/hook.h>
#include <rex/types.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>

namespace rex::kernel::xboxkrnl {

namespace {
constexpr u32 kUsbcamNotReady = X_RESULT_FROM_WIN32(0x15);
}

u32 XUsbcamCreate_entry(u32 buffer,
                        u32 buffer_size,  // 0x4B000 640x480?
                        mapped_void unk3_ptr) {
  (void)buffer;
  (void)buffer_size;
  (void)unk3_ptr;

  // This function should return success.
  // It looks like it only allocates space for usbcam support.
  // returning error code might cause games to initialize incorrectly.
  // "Carcassonne" initalization function checks for result from this
  // function. If value is different than 0 instead of loading
  // rest of the game it returns from initalization function and tries
  // to run game normally which causes crash, due to uninitialized data.
  return X_STATUS_SUCCESS;
}

u32 XUsbcamGetState_entry() {
  // 0 = not connected.
  return 0;
}

u32 XUsbcamDestroy_entry(u32 handle) {
  (void)handle;
  return X_STATUS_SUCCESS;
}

u32 XUsbcamReset_entry(u32 handle) {
  (void)handle;
  return X_STATUS_SUCCESS;
}

u32 XUsbcamSetCaptureMode_entry(u32 handle, u32 mode, u32 format, u32 width, u32 height,
                                u32 pitch, mapped_void config_ptr, mapped_void overlapped_ptr,
                                mapped_void token_ptr) {
  (void)handle;
  (void)mode;
  (void)format;
  (void)width;
  (void)height;
  (void)pitch;
  (void)config_ptr;
  (void)overlapped_ptr;
  (void)token_ptr;
  return kUsbcamNotReady;
}

u32 XUsbcamGetConfig_entry(u32 handle, u32 category, u32 control, mapped_void value_ptr,
                           mapped_u32 value_size_ptr) {
  (void)handle;
  (void)category;
  (void)control;
  (void)value_ptr;
  (void)value_size_ptr;
  return kUsbcamNotReady;
}

u32 XUsbcamSetConfig_entry(u32 handle, u32 category, u32 control, mapped_void value_ptr,
                           u32 value_size, mapped_void defaults_ptr, mapped_void overlapped_ptr) {
  (void)handle;
  (void)category;
  (void)control;
  (void)value_ptr;
  (void)value_size;
  (void)defaults_ptr;
  (void)overlapped_ptr;
  return kUsbcamNotReady;
}

u32 XUsbcamReadFrame_entry(u32 handle, u32 frame, u32 buffer, u32 buffer_size,
                           mapped_void info_ptr, mapped_void overlapped_ptr) {
  (void)handle;
  (void)frame;
  (void)buffer;
  (void)buffer_size;
  (void)info_ptr;
  (void)overlapped_ptr;
  return kUsbcamNotReady;
}

u32 XUsbcamSnapshot_entry(u32 handle, mapped_void buffer, mapped_u32 buffer_size_ptr) {
  (void)handle;
  (void)buffer;
  (void)buffer_size_ptr;
  return kUsbcamNotReady;
}

u32 XUsbcamSetView_entry(u32 handle, u32 view, u32 flags, u32 buffer, mapped_void token_ptr) {
  (void)handle;
  (void)view;
  (void)flags;
  (void)buffer;
  (void)token_ptr;
  return kUsbcamNotReady;
}

u32 XUsbcamGetView_entry(u32 handle, mapped_void view_ptr) {
  (void)handle;
  (void)view_ptr;
  return kUsbcamNotReady;
}

}  // namespace rex::kernel::xboxkrnl

REX_EXPORT(__imp__XUsbcamCreate, rex::kernel::xboxkrnl::XUsbcamCreate_entry)
REX_EXPORT(__imp__XUsbcamGetState, rex::kernel::xboxkrnl::XUsbcamGetState_entry)

REX_EXPORT(__imp__XUsbcamSetCaptureMode, rex::kernel::xboxkrnl::XUsbcamSetCaptureMode_entry)
REX_EXPORT(__imp__XUsbcamGetConfig, rex::kernel::xboxkrnl::XUsbcamGetConfig_entry)
REX_EXPORT(__imp__XUsbcamSetConfig, rex::kernel::xboxkrnl::XUsbcamSetConfig_entry)
REX_EXPORT(__imp__XUsbcamReadFrame, rex::kernel::xboxkrnl::XUsbcamReadFrame_entry)
REX_EXPORT(__imp__XUsbcamSnapshot, rex::kernel::xboxkrnl::XUsbcamSnapshot_entry)
REX_EXPORT(__imp__XUsbcamSetView, rex::kernel::xboxkrnl::XUsbcamSetView_entry)
REX_EXPORT(__imp__XUsbcamGetView, rex::kernel::xboxkrnl::XUsbcamGetView_entry)
REX_EXPORT(__imp__XUsbcamDestroy, rex::kernel::xboxkrnl::XUsbcamDestroy_entry)
REX_EXPORT(__imp__XUsbcamReset, rex::kernel::xboxkrnl::XUsbcamReset_entry)
