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

const std::string& MnkInputDriver::BindingCvarValue(Binding binding) const {
  switch (binding) {
    case Binding::kA:
      return REXCVAR_GET(keybind_a);
    case Binding::kB:
      return REXCVAR_GET(keybind_b);
    case Binding::kX:
      return REXCVAR_GET(keybind_x);
    case Binding::kY:
      return REXCVAR_GET(keybind_y);
    case Binding::kLeftTrigger:
      return REXCVAR_GET(keybind_left_trigger);
    case Binding::kRightTrigger:
      return REXCVAR_GET(keybind_right_trigger);
    case Binding::kLeftShoulder:
      return REXCVAR_GET(keybind_left_shoulder);
    case Binding::kRightShoulder:
      return REXCVAR_GET(keybind_right_shoulder);
    case Binding::kLeftStickUp:
      return REXCVAR_GET(keybind_lstick_up);
    case Binding::kLeftStickDown:
      return REXCVAR_GET(keybind_lstick_down);
    case Binding::kLeftStickLeft:
      return REXCVAR_GET(keybind_lstick_left);
    case Binding::kLeftStickRight:
      return REXCVAR_GET(keybind_lstick_right);
    case Binding::kLeftStickPress:
      return REXCVAR_GET(keybind_lstick_press);
    case Binding::kRightStickPress:
      return REXCVAR_GET(keybind_rstick_press);
    case Binding::kDpadUp:
      return REXCVAR_GET(keybind_dpad_up);
    case Binding::kDpadDown:
      return REXCVAR_GET(keybind_dpad_down);
    case Binding::kDpadLeft:
      return REXCVAR_GET(keybind_dpad_left);
    case Binding::kDpadRight:
      return REXCVAR_GET(keybind_dpad_right);
    case Binding::kBack:
      return REXCVAR_GET(keybind_back);
    case Binding::kStart:
      return REXCVAR_GET(keybind_start);
    case Binding::kGuide:
    case Binding::kCount:
      return REXCVAR_GET(keybind_guide);
  }
  return REXCVAR_GET(keybind_guide);
}

void MnkInputDriver::RefreshBindingsLocked() {
  bool bindings_changed = !bindings_initialized_;
  for (size_t i = 0; i < kBindingCount; ++i) {
    Binding binding = static_cast<Binding>(i);
    const std::string& cvar_value = BindingCvarValue(binding);
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
  for (size_t i = 0; i < kKeystrokeBindings.size(); ++i) {
    const auto& entry = kKeystrokeBindings[i];
    const uint16_t host_key = binding_keys_[static_cast<size_t>(entry.binding)];
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
  auto add_button_if_pressed = [&](Binding binding, uint16_t mask) {
    if (IsBindingPressed(binding)) {
      buttons |= mask;
    }
  };
  add_button_if_pressed(Binding::kA, X_INPUT_GAMEPAD_A);
  add_button_if_pressed(Binding::kB, X_INPUT_GAMEPAD_B);
  add_button_if_pressed(Binding::kX, X_INPUT_GAMEPAD_X);
  add_button_if_pressed(Binding::kY, X_INPUT_GAMEPAD_Y);
  add_button_if_pressed(Binding::kLeftShoulder, X_INPUT_GAMEPAD_LEFT_SHOULDER);
  add_button_if_pressed(Binding::kRightShoulder, X_INPUT_GAMEPAD_RIGHT_SHOULDER);
  add_button_if_pressed(Binding::kLeftStickPress, X_INPUT_GAMEPAD_LEFT_THUMB);
  add_button_if_pressed(Binding::kRightStickPress, X_INPUT_GAMEPAD_RIGHT_THUMB);
  add_button_if_pressed(Binding::kBack, X_INPUT_GAMEPAD_BACK);
  add_button_if_pressed(Binding::kStart, X_INPUT_GAMEPAD_START);
  add_button_if_pressed(Binding::kGuide, X_INPUT_GAMEPAD_GUIDE);
  add_button_if_pressed(Binding::kDpadUp, X_INPUT_GAMEPAD_DPAD_UP);
  add_button_if_pressed(Binding::kDpadDown, X_INPUT_GAMEPAD_DPAD_DOWN);
  add_button_if_pressed(Binding::kDpadLeft, X_INPUT_GAMEPAD_DPAD_LEFT);
  add_button_if_pressed(Binding::kDpadRight, X_INPUT_GAMEPAD_DPAD_RIGHT);

  uint8_t lt = IsBindingPressed(Binding::kLeftTrigger) ? 0xFF : 0;
  uint8_t rt = IsBindingPressed(Binding::kRightTrigger) ? 0xFF : 0;

  int32_t lx = 0;
  int32_t ly = 0;
  if (IsBindingPressed(Binding::kLeftStickLeft)) {
    lx -= INT16_MAX;
  }
  if (IsBindingPressed(Binding::kLeftStickRight)) {
    lx += INT16_MAX;
  }
  if (IsBindingPressed(Binding::kLeftStickUp)) {
    ly += INT16_MAX;
  }
  if (IsBindingPressed(Binding::kLeftStickDown)) {
    ly -= INT16_MAX;
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

    const bool should_capture = has_focus_ && active;
    if (should_capture && !mouse_captured_) {
      mouse_captured_ = true;
      hide_cursor = true;
      capture_mouse = true;
      mouse_dx_ = 0;
      mouse_dy_ = 0;
    } else if (!should_capture && mouse_captured_) {
      mouse_captured_ = false;
      show_cursor = true;
      release_mouse = true;
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
  for (size_t i = 0; mask && i < kKeystrokeBindings.size(); ++i) {
    const uint32_t bit = uint32_t{1} << i;
    if ((mask & bit) != 0) {
      EnqueueKeystroke(kKeystrokeBindings[i].pad_key, down);
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
