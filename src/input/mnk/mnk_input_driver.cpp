/**
 * @file        input/mnk/mnk_input_driver.cpp
 * @brief       Keyboard/mouse input driver implementation.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#include <rex/input/mnk/mnk_input_driver.h>

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/logging.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>

#include <algorithm>
#include <array>
#include <cstring>

#if REX_PLATFORM_WIN32
#include <rex/ui/window_win.h>
#include <Windows.h>
#endif

REXCVAR_DEFINE_BOOL(mnk_mode, false, "Input", "Enable keyboard/mouse controller emulation");
REXCVAR_DEFINE_INT32(mnk_user_index, 0, "Input", "Controller slot (0-3) for MnK").range(0, 3);
REXCVAR_DEFINE_DOUBLE(mnk_sensitivity, 1.0, "Input", "Mouse sensitivity for right stick")
    .range(0.01, 10.0);

REXCVAR_DEFINE_STRING(keybind_a, "Space", "Input/Keybinds/Controller", "A button");
REXCVAR_DEFINE_STRING(keybind_b, "Shift", "Input/Keybinds/Controller", "B button");
REXCVAR_DEFINE_STRING(keybind_x, "R", "Input/Keybinds/Controller", "X button");
REXCVAR_DEFINE_STRING(keybind_y, "E", "Input/Keybinds/Controller", "Y button");
REXCVAR_DEFINE_STRING(keybind_left_trigger, "RMB", "Input/Keybinds/Controller", "Left trigger");
REXCVAR_DEFINE_STRING(keybind_right_trigger, "LMB", "Input/Keybinds/Controller", "Right trigger");
REXCVAR_DEFINE_STRING(keybind_left_shoulder, "Q", "Input/Keybinds/Controller", "Left shoulder");
REXCVAR_DEFINE_STRING(keybind_right_shoulder, "F", "Input/Keybinds/Controller", "Right shoulder");
REXCVAR_DEFINE_STRING(keybind_lstick_up, "W", "Input/Keybinds/Controller", "Left stick up");
REXCVAR_DEFINE_STRING(keybind_lstick_down, "S", "Input/Keybinds/Controller", "Left stick down");
REXCVAR_DEFINE_STRING(keybind_lstick_left, "A", "Input/Keybinds/Controller", "Left stick left");
REXCVAR_DEFINE_STRING(keybind_lstick_right, "D", "Input/Keybinds/Controller", "Left stick right");
REXCVAR_DEFINE_STRING(keybind_lstick_press, "C", "Input/Keybinds/Controller", "Left stick press");
REXCVAR_DEFINE_STRING(keybind_rstick_press, "MMB", "Input/Keybinds/Controller",
                      "Right stick press");
REXCVAR_DEFINE_STRING(keybind_dpad_up, "Up", "Input/Keybinds/Controller", "D-pad up");
REXCVAR_DEFINE_STRING(keybind_dpad_down, "Down", "Input/Keybinds/Controller", "D-pad down");
REXCVAR_DEFINE_STRING(keybind_dpad_left, "Left", "Input/Keybinds/Controller", "D-pad left");
REXCVAR_DEFINE_STRING(keybind_dpad_right, "Right", "Input/Keybinds/Controller", "D-pad right");
REXCVAR_DEFINE_STRING(keybind_back, "Tab", "Input/Keybinds/Controller", "Back button");
REXCVAR_DEFINE_STRING(keybind_start, "Escape", "Input/Keybinds/Controller", "Start button");
REXCVAR_DEFINE_STRING(keybind_guide, "", "Input/Keybinds/Controller", "Guide button");

namespace rex::input::mnk {

using rex::ui::VirtualKey;

namespace {

constexpr uint16_t kNoHostKey = static_cast<uint16_t>(VirtualKey::kNone);
constexpr uint16_t kNoPadKey = 0;

const std::string& KeybindA() {
  return REXCVAR_GET(keybind_a);
}

const std::string& KeybindB() {
  return REXCVAR_GET(keybind_b);
}

const std::string& KeybindX() {
  return REXCVAR_GET(keybind_x);
}

const std::string& KeybindY() {
  return REXCVAR_GET(keybind_y);
}

const std::string& KeybindLeftTrigger() {
  return REXCVAR_GET(keybind_left_trigger);
}

const std::string& KeybindRightTrigger() {
  return REXCVAR_GET(keybind_right_trigger);
}

const std::string& KeybindLeftShoulder() {
  return REXCVAR_GET(keybind_left_shoulder);
}

const std::string& KeybindRightShoulder() {
  return REXCVAR_GET(keybind_right_shoulder);
}

const std::string& KeybindLeftStickUp() {
  return REXCVAR_GET(keybind_lstick_up);
}

const std::string& KeybindLeftStickDown() {
  return REXCVAR_GET(keybind_lstick_down);
}

const std::string& KeybindLeftStickLeft() {
  return REXCVAR_GET(keybind_lstick_left);
}

const std::string& KeybindLeftStickRight() {
  return REXCVAR_GET(keybind_lstick_right);
}

const std::string& KeybindLeftStickPress() {
  return REXCVAR_GET(keybind_lstick_press);
}

const std::string& KeybindRightStickPress() {
  return REXCVAR_GET(keybind_rstick_press);
}

const std::string& KeybindDpadUp() {
  return REXCVAR_GET(keybind_dpad_up);
}

const std::string& KeybindDpadDown() {
  return REXCVAR_GET(keybind_dpad_down);
}

const std::string& KeybindDpadLeft() {
  return REXCVAR_GET(keybind_dpad_left);
}

const std::string& KeybindDpadRight() {
  return REXCVAR_GET(keybind_dpad_right);
}

const std::string& KeybindBack() {
  return REXCVAR_GET(keybind_back);
}

const std::string& KeybindStart() {
  return REXCVAR_GET(keybind_start);
}

const std::string& KeybindGuide() {
  return REXCVAR_GET(keybind_guide);
}

constexpr uint16_t PadKey(VirtualKey key) {
  return static_cast<uint16_t>(key);
}

uint16_t ParseHostBinding(const std::string& cvar_val) {
  VirtualKey vk = rex::ui::ParseVirtualKey(cvar_val);
  uint16_t host_key = static_cast<uint16_t>(vk);
  return vk != VirtualKey::kNone && host_key < 256 ? host_key : kNoHostKey;
}

uint16_t MouseButtonToVirtualKey(rex::ui::MouseEvent::Button button) {
  switch (button) {
    case rex::ui::MouseEvent::Button::kLeft:
      return static_cast<uint16_t>(VirtualKey::kLButton);
    case rex::ui::MouseEvent::Button::kRight:
      return static_cast<uint16_t>(VirtualKey::kRButton);
    case rex::ui::MouseEvent::Button::kMiddle:
      return static_cast<uint16_t>(VirtualKey::kMButton);
    default:
      return kNoHostKey;
  }
}

}  // namespace

MnkInputDriver::MnkInputDriver(rex::ui::Window* window, size_t window_z_order)
    : InputDriver(window, window_z_order) {}

MnkInputDriver::~MnkInputDriver() {
  // Detach handled by OnClosing; if window outlives the driver, clean up here.
  rex::ui::Window* window = nullptr;
  bool release_mouse = false;
  {
    std::lock_guard lock(state_mutex_);
    window = attached_window_;
    release_mouse = mouse_captured_;
    mouse_captured_ = false;
    mouse_position_initialized_ = false;
    attached_window_ = nullptr;
  }

  if (window) {
    if (release_mouse) {
      window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
      window->ReleaseMouse();
    }
    window->RemoveInputListener(this);
    window->RemoveListener(this);
  }
}

X_STATUS MnkInputDriver::Setup() {
  REXLOG_INFO("MnK input driver initialized");
  return X_STATUS_SUCCESS;
}

void MnkInputDriver::OnWindowAvailable(rex::ui::Window* window) {
  if (window) {
    {
      std::lock_guard lock(state_mutex_);
      attached_window_ = window;
    }
    window->AddInputListener(this, window_z_order());
    window->AddListener(this);
  }
}

void MnkInputDriver::OnClosing(rex::ui::UIEvent&) {
  rex::ui::Window* window = nullptr;
  bool release_mouse = false;
  {
    std::lock_guard lock(state_mutex_);
    window = attached_window_;
    release_mouse = mouse_captured_;
    mouse_captured_ = false;
    mouse_position_initialized_ = false;
    attached_window_ = nullptr;
  }

  if (window) {
    if (release_mouse) {
      window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
      window->ReleaseMouse();
    }
    window->RemoveInputListener(this);
    window->RemoveListener(this);
  }
}

uint32_t MnkInputDriver::UserIndex() const {
  return static_cast<uint32_t>(REXCVAR_GET(mnk_user_index));
}

bool MnkInputDriver::IsEnabled() const {
  return REXCVAR_GET(mnk_mode);
}

auto MnkInputDriver::BindingMetadataTable() -> const std::array<BindingMetadata, kBindingCount>& {
  static const std::array<BindingMetadata, kBindingCount> kBindings = {{
      {KeybindA, X_INPUT_GAMEPAD_A, PadKey(VirtualKey::kXInputPadA), AnalogTarget::kNone, 0},
      {KeybindB, X_INPUT_GAMEPAD_B, PadKey(VirtualKey::kXInputPadB), AnalogTarget::kNone, 0},
      {KeybindX, X_INPUT_GAMEPAD_X, PadKey(VirtualKey::kXInputPadX), AnalogTarget::kNone, 0},
      {KeybindY, X_INPUT_GAMEPAD_Y, PadKey(VirtualKey::kXInputPadY), AnalogTarget::kNone, 0},
      {KeybindLeftTrigger, 0, PadKey(VirtualKey::kXInputPadLTrigger), AnalogTarget::kLeftTrigger,
       0xFF},
      {KeybindRightTrigger, 0, PadKey(VirtualKey::kXInputPadRTrigger), AnalogTarget::kRightTrigger,
       0xFF},
      {KeybindLeftShoulder, X_INPUT_GAMEPAD_LEFT_SHOULDER, PadKey(VirtualKey::kXInputPadLShoulder),
       AnalogTarget::kNone, 0},
      {KeybindRightShoulder, X_INPUT_GAMEPAD_RIGHT_SHOULDER,
       PadKey(VirtualKey::kXInputPadRShoulder), AnalogTarget::kNone, 0},
      {KeybindLeftStickUp, 0, PadKey(VirtualKey::kXInputPadLThumbUp), AnalogTarget::kLeftStickY,
       INT16_MAX},
      {KeybindLeftStickDown, 0, PadKey(VirtualKey::kXInputPadLThumbDown), AnalogTarget::kLeftStickY,
       -INT16_MAX},
      {KeybindLeftStickLeft, 0, PadKey(VirtualKey::kXInputPadLThumbLeft), AnalogTarget::kLeftStickX,
       -INT16_MAX},
      {KeybindLeftStickRight, 0, PadKey(VirtualKey::kXInputPadLThumbRight),
       AnalogTarget::kLeftStickX, INT16_MAX},
      {KeybindLeftStickPress, X_INPUT_GAMEPAD_LEFT_THUMB, PadKey(VirtualKey::kXInputPadLThumbPress),
       AnalogTarget::kNone, 0},
      {KeybindRightStickPress, X_INPUT_GAMEPAD_RIGHT_THUMB,
       PadKey(VirtualKey::kXInputPadRThumbPress), AnalogTarget::kNone, 0},
      {KeybindDpadUp, X_INPUT_GAMEPAD_DPAD_UP, PadKey(VirtualKey::kXInputPadDpadUp),
       AnalogTarget::kNone, 0},
      {KeybindDpadDown, X_INPUT_GAMEPAD_DPAD_DOWN, PadKey(VirtualKey::kXInputPadDpadDown),
       AnalogTarget::kNone, 0},
      {KeybindDpadLeft, X_INPUT_GAMEPAD_DPAD_LEFT, PadKey(VirtualKey::kXInputPadDpadLeft),
       AnalogTarget::kNone, 0},
      {KeybindDpadRight, X_INPUT_GAMEPAD_DPAD_RIGHT, PadKey(VirtualKey::kXInputPadDpadRight),
       AnalogTarget::kNone, 0},
      {KeybindBack, X_INPUT_GAMEPAD_BACK, PadKey(VirtualKey::kXInputPadBack), AnalogTarget::kNone,
       0},
      {KeybindStart, X_INPUT_GAMEPAD_START, PadKey(VirtualKey::kXInputPadStart),
       AnalogTarget::kNone, 0},
      {KeybindGuide, X_INPUT_GAMEPAD_GUIDE, kNoPadKey, AnalogTarget::kNone, 0},
  }};
  return kBindings;
}

void MnkInputDriver::RefreshBindingsLocked() {
  bool bindings_changed = !bindings_initialized_;
  const auto& bindings = BindingMetadataTable();
  for (size_t i = 0; i < kBindingCount; ++i) {
    const std::string& cvar_value = bindings[i].cvar_value();
    if (!bindings_initialized_ || binding_values_[i] != cvar_value) {
      binding_values_[i] = cvar_value;
      binding_keys_[i] = ParseHostBinding(cvar_value);
      bindings_changed = true;
    }
  }
  if (bindings_changed) {
    RebuildKeystrokeLookupLocked();
  }
  bindings_initialized_ = true;
}

void MnkInputDriver::RebuildKeystrokeLookupLocked() {
  keystroke_masks_by_host_key_.fill(0);
  const auto& bindings = BindingMetadataTable();
  for (size_t i = 0; i < bindings.size(); ++i) {
    if (bindings[i].pad_key == kNoPadKey) {
      continue;
    }
    const uint16_t host_key = binding_keys_[i];
    if (host_key != kNoHostKey && host_key < keystroke_masks_by_host_key_.size()) {
      keystroke_masks_by_host_key_[host_key] |= uint32_t{1} << i;
    }
  }
}

bool MnkInputDriver::IsBindingPressed(Binding binding) const {
  uint16_t key = binding_keys_[static_cast<size_t>(binding)];
  return key != kNoHostKey && key < 256 && key_down_[key];
}

X_RESULT MnkInputDriver::GetCapabilities(uint32_t user_index, uint32_t flags,
                                         X_INPUT_CAPABILITIES* out_caps) {
  (void)flags;
  if (!IsEnabled() || user_index != UserIndex()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (out_caps) {
    std::memset(out_caps, 0, sizeof(*out_caps));
    out_caps->type = 0x01;
    out_caps->sub_type = 0x01;
    out_caps->flags = 0;
    out_caps->gamepad.buttons = 0xFFFF;
    out_caps->gamepad.left_trigger = 0xFF;
    out_caps->gamepad.right_trigger = 0xFF;
    out_caps->gamepad.thumb_lx = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_ly = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_rx = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_ry = static_cast<int16_t>(0x7FFF);
    out_caps->vibration.left_motor_speed = 0xFFFF;
    out_caps->vibration.right_motor_speed = 0xFFFF;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT MnkInputDriver::GetState(uint32_t user_index, X_INPUT_STATE* out_state) {
  if (!IsEnabled() || user_index != UserIndex()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  const bool active = is_active();
  UpdateMouseCapture(active);

  std::lock_guard lock(state_mutex_);
  if (!active || !has_focus_) {
    if (out_state) {
      std::memset(out_state, 0, sizeof(*out_state));
      out_state->packet_number = packet_number_;
    }
    return X_ERROR_SUCCESS;
  }

  RefreshBindingsLocked();

  uint16_t buttons = 0;
  uint8_t lt = 0;
  uint8_t rt = 0;
  int32_t lx = 0;
  int32_t ly = 0;
  const auto& bindings = BindingMetadataTable();
  for (size_t i = 0; i < bindings.size(); ++i) {
    if (!IsBindingPressed(static_cast<Binding>(i))) {
      continue;
    }

    const auto& binding = bindings[i];
    buttons |= binding.button_mask;
    switch (binding.analog_target) {
      case AnalogTarget::kLeftTrigger:
        lt = std::max(lt, static_cast<uint8_t>(binding.analog_value));
        break;
      case AnalogTarget::kRightTrigger:
        rt = std::max(rt, static_cast<uint8_t>(binding.analog_value));
        break;
      case AnalogTarget::kLeftStickX:
        lx += binding.analog_value;
        break;
      case AnalogTarget::kLeftStickY:
        ly += binding.analog_value;
        break;
      case AnalogTarget::kNone:
        break;
    }
  }

  double sensitivity = REXCVAR_GET(mnk_sensitivity);
  constexpr double kBaseScale = 200.0;
  int32_t rx = static_cast<int32_t>(mouse_dx_ * sensitivity * kBaseScale);
  int32_t ry = static_cast<int32_t>(-mouse_dy_ * sensitivity * kBaseScale);
  mouse_dx_ = 0;
  mouse_dy_ = 0;

  auto clamp16 = [](int32_t v) -> int16_t {
    return static_cast<int16_t>(std::clamp(v, int32_t{INT16_MIN}, int32_t{INT16_MAX}));
  };

  packet_number_++;

  if (out_state) {
    out_state->packet_number = packet_number_;
    out_state->gamepad.buttons = buttons;
    out_state->gamepad.left_trigger = lt;
    out_state->gamepad.right_trigger = rt;
    out_state->gamepad.thumb_lx = clamp16(lx);
    out_state->gamepad.thumb_ly = clamp16(ly);
    out_state->gamepad.thumb_rx = clamp16(rx);
    out_state->gamepad.thumb_ry = clamp16(ry);
  }
  return X_ERROR_SUCCESS;
}

X_RESULT MnkInputDriver::SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) {
  (void)vibration;
  if (!IsEnabled() || user_index != UserIndex()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT MnkInputDriver::GetKeystroke(uint32_t user_index, uint32_t flags,
                                      X_INPUT_KEYSTROKE* out_keystroke) {
  (void)flags;
  if (!IsEnabled() || user_index != UserIndex()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  std::lock_guard lock(state_mutex_);
  if (keystroke_queue_.empty()) {
    return X_ERROR_EMPTY;
  }
  if (out_keystroke) {
    *out_keystroke = keystroke_queue_.front();
  }
  keystroke_queue_.pop();
  return X_ERROR_SUCCESS;
}

void MnkInputDriver::EnqueueKeystroke(uint16_t vk_pad, bool down) {
  X_INPUT_KEYSTROKE ks = {};
  ks.virtual_key = vk_pad;
  ks.unicode = 0;
  ks.flags = down ? X_INPUT_KEYSTROKE_KEYDOWN : X_INPUT_KEYSTROKE_KEYUP;
  ks.user_index = static_cast<uint8_t>(UserIndex());
  ks.hid_code = 0;
  keystroke_queue_.push(ks);
}

void MnkInputDriver::CenterCursor(rex::ui::Window* window, int32_t x, int32_t y) {
  if (!window) {
    return;
  }
#if REX_PLATFORM_WIN32
  auto* win32_window = dynamic_cast<rex::ui::Win32Window*>(window);
  if (win32_window && win32_window->hwnd()) {
    POINT pt = {static_cast<LONG>(x), static_cast<LONG>(y)};
    ClientToScreen(win32_window->hwnd(), &pt);
    SetCursorPos(pt.x, pt.y);
  }
#endif
}

void MnkInputDriver::UpdateMouseCapture(bool active) {
  rex::ui::Window* window = nullptr;
  bool hide_cursor = false;
  bool show_cursor = false;
  bool capture_mouse = false;
  bool release_mouse = false;
  bool center_cursor = false;

  {
    std::lock_guard lock(state_mutex_);
    window = attached_window_;
    if (!window) {
      return;
    }

    const bool should_capture = has_focus_ && active && window->IsFullscreen();
    if (should_capture && !mouse_captured_) {
      mouse_captured_ = true;
      hide_cursor = true;
      capture_mouse = true;
      mouse_dx_ = 0;
      mouse_dy_ = 0;
      mouse_position_initialized_ = false;
    } else if (!should_capture && mouse_captured_) {
      mouse_captured_ = false;
      show_cursor = true;
      release_mouse = true;
      mouse_position_initialized_ = false;
    }
    center_cursor = mouse_captured_;
  }

  if (hide_cursor) {
    window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
  }
  if (capture_mouse) {
    window->CaptureMouse();
  }
  if (show_cursor) {
    window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
  }
  if (release_mouse) {
    window->ReleaseMouse();
  }

  if (center_cursor) {
    int32_t center_x = static_cast<int32_t>(window->GetActualLogicalWidth() / 2);
    int32_t center_y = static_cast<int32_t>(window->GetActualLogicalHeight() / 2);
    {
      std::lock_guard lock(state_mutex_);
      if (attached_window_ != window || !mouse_captured_) {
        return;
      }
      prev_mouse_x_ = center_x;
      prev_mouse_y_ = center_y;
      mouse_position_initialized_ = true;
    }
    CenterCursor(window, center_x, center_y);
  }
}

bool MnkInputDriver::SetKeyState(uint16_t vk, bool down) {
  if (vk == static_cast<uint16_t>(VirtualKey::kNone) || vk >= 256) {
    return false;
  }
  bool changed = key_down_[vk] != down;
  key_down_[vk] = down;
  return changed;
}

void MnkInputDriver::EnqueueBoundKeystrokes(uint16_t vk, bool down) {
  RefreshBindingsLocked();
  if (vk >= keystroke_masks_by_host_key_.size()) {
    return;
  }

  uint32_t mask = keystroke_masks_by_host_key_[vk];
  const auto& bindings = BindingMetadataTable();
  for (size_t i = 0; mask && i < bindings.size(); ++i) {
    const uint32_t bit = uint32_t{1} << i;
    if ((mask & bit) != 0) {
      EnqueueKeystroke(bindings[i].pad_key, down);
      mask &= ~bit;
    }
  }
}

void MnkInputDriver::OnKeyDown(rex::ui::KeyEvent& e) {
  if (!IsEnabled()) {
    return;
  }
  std::lock_guard lock(state_mutex_);
  if (!has_focus_) {
    return;
  }
  uint16_t vk = static_cast<uint16_t>(e.virtual_key());
  if (SetKeyState(vk, true)) {
    EnqueueBoundKeystrokes(vk, true);
  }
}

void MnkInputDriver::OnKeyUp(rex::ui::KeyEvent& e) {
  if (!IsEnabled())
    return;
  std::lock_guard lock(state_mutex_);
  uint16_t vk = static_cast<uint16_t>(e.virtual_key());
  if (SetKeyState(vk, false)) {
    EnqueueBoundKeystrokes(vk, false);
  }
}

void MnkInputDriver::OnMouseDown(rex::ui::MouseEvent& e) {
  if (!IsEnabled()) {
    return;
  }
  uint16_t vk = MouseButtonToVirtualKey(e.button());
  if (vk == kNoHostKey) {
    return;
  }
  std::lock_guard lock(state_mutex_);
  if (!has_focus_) {
    return;
  }
  if (SetKeyState(vk, true)) {
    EnqueueBoundKeystrokes(vk, true);
  }
}

void MnkInputDriver::OnMouseUp(rex::ui::MouseEvent& e) {
  if (!IsEnabled())
    return;
  uint16_t vk = MouseButtonToVirtualKey(e.button());
  if (vk == kNoHostKey) {
    return;
  }
  std::lock_guard lock(state_mutex_);
  if (SetKeyState(vk, false)) {
    EnqueueBoundKeystrokes(vk, false);
  }
}

void MnkInputDriver::OnMouseMove(rex::ui::MouseEvent& e) {
  if (!IsEnabled()) {
    return;
  }
  std::lock_guard lock(state_mutex_);
  if (!has_focus_) {
    return;
  }
  int32_t x = e.x();
  int32_t y = e.y();
  if (!mouse_position_initialized_) {
    prev_mouse_x_ = x;
    prev_mouse_y_ = y;
    mouse_position_initialized_ = true;
    return;
  }
  mouse_dx_ += x - prev_mouse_x_;
  mouse_dy_ += y - prev_mouse_y_;
  prev_mouse_x_ = x;
  prev_mouse_y_ = y;
}

void MnkInputDriver::OnLostFocus(rex::ui::UISetupEvent&) {
  rex::ui::Window* window = nullptr;
  bool release_mouse = false;
  {
    std::lock_guard lock(state_mutex_);
    has_focus_ = false;
    std::memset(key_down_, 0, sizeof(key_down_));
    keystroke_queue_ = {};
    mouse_dx_ = 0;
    mouse_dy_ = 0;
    mouse_position_initialized_ = false;
    window = attached_window_;
    release_mouse = mouse_captured_ && window;
    mouse_captured_ = false;
  }

  if (release_mouse) {
    window->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
    window->ReleaseMouse();
  }
}

void MnkInputDriver::OnGotFocus(rex::ui::UISetupEvent&) {
  std::lock_guard lock(state_mutex_);
  has_focus_ = true;
}

}  // namespace rex::input::mnk
