/**
 * @file        rex/input/mnk/mnk_input_driver.h
 * @brief       Keyboard/mouse input driver - maps MnK to Xbox 360 controller.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/input/input_driver.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window_listener.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>

namespace rex::input::mnk {

class MnkInputDriver final : public InputDriver,
                             public rex::ui::WindowInputListener,
                             public rex::ui::WindowListener {
 public:
  explicit MnkInputDriver(rex::ui::Window* window, size_t window_z_order);
  ~MnkInputDriver() override;

  X_STATUS Setup() override;

  X_RESULT GetCapabilities(uint32_t user_index, uint32_t flags,
                           X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* out_state) override;
  X_RESULT SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags,
                        X_INPUT_KEYSTROKE* out_keystroke) override;

  void OnWindowAvailable(rex::ui::Window* window) override;

  // WindowInputListener
  void OnKeyDown(rex::ui::KeyEvent& e) override;
  void OnKeyUp(rex::ui::KeyEvent& e) override;
  void OnMouseDown(rex::ui::MouseEvent& e) override;
  void OnMouseUp(rex::ui::MouseEvent& e) override;
  void OnMouseMove(rex::ui::MouseEvent& e) override;

  // WindowListener
  void OnClosing(rex::ui::UIEvent& e) override;
  void OnLostFocus(rex::ui::UISetupEvent& e) override;
  void OnGotFocus(rex::ui::UISetupEvent& e) override;

 private:
  enum class Binding : size_t {
    kA,
    kB,
    kX,
    kY,
    kLeftTrigger,
    kRightTrigger,
    kLeftShoulder,
    kRightShoulder,
    kLeftStickUp,
    kLeftStickDown,
    kLeftStickLeft,
    kLeftStickRight,
    kLeftStickPress,
    kRightStickPress,
    kDpadUp,
    kDpadDown,
    kDpadLeft,
    kDpadRight,
    kBack,
    kStart,
    kGuide,
    kCount,
  };
  static constexpr size_t kBindingCount = static_cast<size_t>(Binding::kCount);

  struct KeystrokeBinding {
    Binding binding;
    uint16_t pad_key;
  };

  static constexpr std::array<KeystrokeBinding, 20> kKeystrokeBindings = {{
      {Binding::kA, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadA)},
      {Binding::kB, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadB)},
      {Binding::kX, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadX)},
      {Binding::kY, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadY)},
      {Binding::kLeftTrigger,
       static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadLTrigger)},
      {Binding::kRightTrigger,
       static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadRTrigger)},
      {Binding::kLeftShoulder,
       static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadLShoulder)},
      {Binding::kRightShoulder,
       static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadRShoulder)},
      {Binding::kLeftStickPress,
       static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadLThumbPress)},
      {Binding::kRightStickPress,
       static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadRThumbPress)},
      {Binding::kDpadUp, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadDpadUp)},
      {Binding::kDpadDown, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadDpadDown)},
      {Binding::kDpadLeft, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadDpadLeft)},
      {Binding::kDpadRight,
       static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadDpadRight)},
      {Binding::kBack, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadBack)},
      {Binding::kStart, static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadStart)},
      {Binding::kLeftStickUp,
       static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadLThumbUp)},
      {Binding::kLeftStickDown,
       static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadLThumbDown)},
      {Binding::kLeftStickLeft,
       static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadLThumbLeft)},
      {Binding::kLeftStickRight,
       static_cast<uint16_t>(rex::ui::VirtualKey::kXInputPadLThumbRight)},
  }};
  static_assert(kKeystrokeBindings.size() <= 32);

  uint32_t UserIndex() const;
  bool IsEnabled() const;
  const std::string& BindingCvarValue(Binding binding) const;
  void RefreshBindingsLocked();
  void RebuildKeystrokeLookupLocked();
  bool IsBindingPressed(Binding binding) const;
  void CenterCursor(rex::ui::Window* window, int32_t x, int32_t y);
  void UpdateMouseCapture(bool active);
  bool SetKeyState(uint16_t vk, bool down);
  void EnqueueBoundKeystrokes(uint16_t vk, bool down);
  void EnqueueKeystroke(uint16_t vk_pad, bool down);

  rex::ui::Window* attached_window_ = nullptr;

  std::mutex state_mutex_;
  bool key_down_[256] = {};
  bool bindings_initialized_ = false;
  std::array<std::string, kBindingCount> binding_values_;
  std::array<uint16_t, kBindingCount> binding_keys_ = {};
  std::array<uint32_t, 256> keystroke_masks_by_host_key_ = {};

  // Mouse delta tracking
  int32_t mouse_dx_ = 0;
  int32_t mouse_dy_ = 0;
  int32_t prev_mouse_x_ = 0;
  int32_t prev_mouse_y_ = 0;
  bool mouse_captured_ = false;
  bool has_focus_ = true;

  // Keystroke queue
  std::queue<X_INPUT_KEYSTROKE> keystroke_queue_;

  // Packet number incremented for each state poll.
  uint32_t packet_number_ = 0;
};

}  // namespace rex::input::mnk
