#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/logging/sink.h>
#include <rex/system/xtypes.h>
#include <rex/system/xio.h>
#include <rex/types.h>

namespace rex::kernel::xboxkrnl {
u32 IoDismountVolumeByFileHandle_entry(u32 handle);
u32 IoDismountVolumeByName_entry(ppc_ptr_t<rex::system::X_ANSI_STRING> name);
u32 StfsCreateDevice_entry(mapped_void device_object, u32 flags, mapped_u32 out_device);
u32 StfsControlDevice_entry(mapped_void device_object, u32 ioctl, mapped_void input_buffer,
                            u32 input_buffer_size, mapped_void output_buffer,
                            u32 output_buffer_size);
}  // namespace rex::kernel::xboxkrnl

namespace {
bool IsVolumeOrStfsNoOpLog(std::string_view text) {
  return text.find("IoDismountVolumeByFileHandle") != std::string_view::npos ||
         text.find("IoDismountVolumeByName") != std::string_view::npos ||
         text.find("StfsCreateDevice") != std::string_view::npos ||
         text.find("StfsControlDevice") != std::string_view::npos;
}
}  // namespace

TEST_CASE("Volume and STFS no-op helpers are trace-only compatibility shims",
          "[runtime][kernel][xboxkrnl][io]") {
  rex::InitLogging(nullptr, spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::krnl(), spdlog::level::trace);

  const bool old_noisy = REXCVAR_GET(log_noisy);
  REXCVAR_SET(log_noisy, true);

  auto sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::krnl(), sink);

  rex::be_u32 out_device = 0xBEEFBEEFu;
  std::array<uint8_t, 4> input{0x11, 0x22, 0x33, 0x44};
  std::array<uint8_t, 4> output{0xCD, 0xCD, 0xCD, 0xCD};

  REXKRNL_DEBUG("Volume and STFS capture sentinel");
  CHECK(rex::kernel::xboxkrnl::IoDismountVolumeByFileHandle_entry(0xFEEDC0DE) == 0);
  CHECK(rex::kernel::xboxkrnl::IoDismountVolumeByName_entry(
            ppc_ptr_t<rex::system::X_ANSI_STRING>(nullptr)) == 0);
  CHECK(rex::kernel::xboxkrnl::StfsCreateDevice_entry(
            mapped_void(reinterpret_cast<void*>(uintptr_t{0x1}), 0x40001000), 0x58,
            mapped_u32(&out_device, 0x40002000)) == 0);
  CHECK(rex::kernel::xboxkrnl::StfsControlDevice_entry(
            mapped_void(reinterpret_cast<void*>(uintptr_t{0x1}), 0x40001000), 4,
            mapped_void(input.data(), 0x40003000), static_cast<u32>(input.size()),
            mapped_void(output.data(), 0x40004000), static_cast<u32>(output.size())) == 0);
  CHECK(out_device == 0xBEEFBEEFu);
  CHECK(output == std::array<uint8_t, 4>{0xCD, 0xCD, 0xCD, 0xCD});

  std::vector<rex::LogEntry> entries;
  sink->CopyEntries(entries);
  rex::RemoveSink(rex::log::krnl(), sink);
  REXCVAR_SET(log_noisy, old_noisy);

  CHECK(std::any_of(entries.begin(), entries.end(), [](const rex::LogEntry& entry) {
    return entry.text.find("Volume and STFS capture sentinel") != std::string::npos;
  }));

  const auto warning_count =
      std::count_if(entries.begin(), entries.end(), [](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn && IsVolumeOrStfsNoOpLog(entry.text);
      });
  CHECK(warning_count == 0);
}
