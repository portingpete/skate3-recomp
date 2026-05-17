#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <rex/cvar.h>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/logging/sink.h>
#include <rex/memory.h>
#include <rex/runtime.h>
#include <rex/system/xio.h>
#include <rex/system/xthread.h>
#include <rex/system/xtypes.h>
#include <rex/types.h>

REXCVAR_DECLARE(bool, headless);

namespace rex::kernel::xam {
u32 XamShowMessageBoxUIEx_entry(u32 user_index, mapped_wstring title_ptr,
                                mapped_wstring text_ptr, u32 button_count,
                                mapped_u32 button_ptrs, u32 active_button, u32 flags,
                                u32 unknown_unused, mapped_u32 result_ptr,
                                mapped_void overlapped);
}  // namespace rex::kernel::xam

namespace {

bool IsMessageBoxUiExLog(std::string_view text) {
  return text.find("XamShowMessageBoxUIEx") != std::string_view::npos;
}

u32 StoreUtf16(rex::memory::Memory* memory, const std::u16string& value) {
  const u32 guest = memory->SystemHeapAlloc(static_cast<u32>((value.size() + 1) * sizeof(u16)));
  auto* host = memory->TranslateVirtual<uint16_t*>(guest);
  REQUIRE(host != nullptr);
  rex::memory::store_and_swap<std::u16string>(host, value);
  rex::memory::store_and_swap<uint16_t>(host + value.size(), 0);
  return guest;
}

}  // namespace

TEST_CASE("XamShowMessageBoxUIEx follows headless message-box selection",
          "[runtime][kernel][xam_ui]") {
  constexpr u32 kSuccess = 0;
  constexpr u32 kUserIndexAny = 0xFF;
  constexpr u32 kActiveButton = 1;

  rex::InitLogging(nullptr, spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::krnl(), spdlog::level::trace);

  const bool old_headless = REXCVAR_GET(headless);
  REXCVAR_SET(headless, true);

  rex::Runtime runtime({}, {}, {}, {});
  rex::RuntimeConfig config;
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == 0);

  auto* memory = runtime.kernel_state()->memory();
  const u32 ok_guest = StoreUtf16(memory, u"OK");
  const u32 cancel_guest = StoreUtf16(memory, u"Cancel");

  const u32 button_ptrs_guest = memory->SystemHeapAlloc(2 * sizeof(u32));
  auto* button_ptrs = memory->TranslateVirtual<rex::be_u32*>(button_ptrs_guest);
  REQUIRE(button_ptrs != nullptr);
  button_ptrs[0] = ok_guest;
  button_ptrs[1] = cancel_guest;

  rex::be_u32 selected_button = 0xFEEDBEEFu;

  auto sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::krnl(), sink);

  CHECK(rex::kernel::xam::XamShowMessageBoxUIEx_entry(
            kUserIndexAny, mapped_wstring(nullptr), mapped_wstring(nullptr), 2,
            mapped_u32(button_ptrs, button_ptrs_guest), kActiveButton, 1, 0x12345678,
            mapped_u32(&selected_button, 0x40001000), mapped_void(nullptr)) == kSuccess);
  CHECK(static_cast<u32>(selected_button) == kActiveButton);

  std::vector<rex::LogEntry> entries;
  sink->CopyEntries(entries);
  rex::RemoveSink(rex::log::krnl(), sink);
  REXCVAR_SET(headless, old_headless);

  const auto warning_count =
      std::count_if(entries.begin(), entries.end(), [](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn && IsMessageBoxUiExLog(entry.text);
      });
  CHECK(warning_count == 0);
}

TEST_CASE("XamShowMessageBoxUIEx preserves overlapped pending shape",
          "[runtime][kernel][xam_ui]") {
  constexpr u32 kIoPending = 0x3E5;
  constexpr u32 kUserIndexAny = 0xFF;
  constexpr rex::X_STATUS kStatusSuccess = 0;

  const bool old_headless = REXCVAR_GET(headless);
  REXCVAR_SET(headless, true);

  rex::Runtime runtime({}, {}, {}, {});
  rex::RuntimeConfig config;
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == 0);

  auto* memory = runtime.kernel_state()->memory();
  const u32 selected_button_guest = memory->SystemHeapAlloc(sizeof(u32));
  auto* selected_button = memory->TranslateVirtual<rex::be_u32*>(selected_button_guest);
  REQUIRE(selected_button != nullptr);
  *selected_button = 0xFEEDBEEFu;

  const u32 overlapped_guest = memory->SystemHeapAlloc(sizeof(rex::system::XAM_OVERLAPPED));
  auto* overlapped =
      memory->TranslateVirtual<rex::system::XAM_OVERLAPPED*>(overlapped_guest);
  REQUIRE(overlapped != nullptr);
  std::memset(overlapped, 0, sizeof(*overlapped));

  std::atomic<u32> call_result = 0xFFFFFFFFu;
  std::atomic<u32> observed_overlapped_result = 0xFFFFFFFFu;
  std::atomic<u32> observed_selected_button = 0xFFFFFFFFu;

  rex::system::object_ref<rex::system::XHostThread> host_thread(new rex::system::XHostThread(
      runtime.kernel_state(), 128 * 1024, rex::system::X_CREATE_SUSPENDED, [&]() {
        call_result = rex::kernel::xam::XamShowMessageBoxUIEx_entry(
            kUserIndexAny, mapped_wstring(nullptr), mapped_wstring(nullptr), 0, mapped_u32(nullptr),
            0, 1, 0x87654321, mapped_u32(selected_button, selected_button_guest),
            mapped_void(overlapped, overlapped_guest));
        observed_overlapped_result = static_cast<u32>(overlapped->result);
        observed_selected_button = static_cast<u32>(*selected_button);
        return 0;
      }));
  REQUIRE(host_thread->Create() == kStatusSuccess);
  REQUIRE(host_thread->Resume() == kStatusSuccess);

  uint64_t wait_timeout = static_cast<uint64_t>(-5'000LL * 10'000LL);
  REQUIRE(host_thread->Wait(0, 0, 0, &wait_timeout) == kStatusSuccess);

  CHECK(call_result == kIoPending);
  CHECK(observed_overlapped_result == kIoPending);
  CHECK(observed_selected_button == 0xFEEDBEEF);

  REXCVAR_SET(headless, old_headless);
}
