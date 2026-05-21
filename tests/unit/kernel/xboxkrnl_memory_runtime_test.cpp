#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <rex/cvar.h>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/logging/sink.h>
#include <rex/runtime.h>
#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::kernel::xboxkrnl {
u32 NtAllocateVirtualMemory_entry(mapped_u32 base_addr_ptr, mapped_u32 region_size_ptr,
                                  u32 alloc_type, u32 protect_bits, u32 debug_memory);
u32 MmAllocatePhysicalMemoryEx_entry(u32 flags, u32 region_size, u32 protect_bits,
                                     u32 min_addr_range, u32 max_addr_range, u32 alignment);
}  // namespace rex::kernel::xboxkrnl

namespace {
bool IsDevkitMemoryHintLog(std::string_view text) {
  return text.find("devkit memory") != std::string_view::npos &&
         text.find("debug_memory=") != std::string_view::npos;
}

bool IsRoundedPhysicalAllocationResultLog(std::string_view text) {
  return text.find("MmAllocatePhysicalMemoryEx") != std::string_view::npos &&
         text.find("addr=0x") != std::string_view::npos &&
         text.find("size=0x1000") != std::string_view::npos;
}
}  // namespace

TEST_CASE("NtAllocateVirtualMemory devkit memory hint is a debug diagnostic",
          "[runtime][kernel][xboxkrnl][memory]") {
  rex::InitLogging(nullptr, spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::krnl(), spdlog::level::trace);

  auto sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::krnl(), sink);

  rex::be_u32 base_addr = 0;
  rex::be_u32 region_size = 0;
  CHECK(rex::kernel::xboxkrnl::NtAllocateVirtualMemory_entry(
            mapped_u32(&base_addr, 0x40001000), mapped_u32(&region_size, 0x40002000),
            rex::X_MEM_COMMIT, rex::X_PAGE_READWRITE, 1) ==
        static_cast<rex::X_STATUS>(0xC000000D));

  std::vector<rex::LogEntry> entries;
  sink->CopyEntries(entries);
  rex::RemoveSink(rex::log::krnl(), sink);

  const auto warning_count =
      std::count_if(entries.begin(), entries.end(), [](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn && IsDevkitMemoryHintLog(entry.text);
      });
  const auto debug_count =
      std::count_if(entries.begin(), entries.end(), [](const rex::LogEntry& entry) {
        return entry.level == spdlog::level::debug && IsDevkitMemoryHintLog(entry.text);
      });

  CHECK(debug_count == 1);
  CHECK(warning_count == 0);
}

TEST_CASE("MmAllocatePhysicalMemoryEx result logs rounded allocation size",
          "[runtime][kernel][xboxkrnl][memory]") {
  const auto root = std::filesystem::temp_directory_path() /
                    ("rex_mm_phys_alloc_log_" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  std::filesystem::create_directories(root);

  rex::Runtime runtime(root, {}, {}, {});
  rex::RuntimeConfig config;
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == static_cast<rex::X_STATUS>(0));

  rex::InitLogging(nullptr, spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::krnl(), spdlog::level::trace);

  const bool old_noisy = REXCVAR_GET(log_noisy);
  REXCVAR_SET(log_noisy, true);

  auto sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::krnl(), sink);

  CHECK(rex::kernel::xboxkrnl::MmAllocatePhysicalMemoryEx_entry(
            0, 0, rex::X_PAGE_READWRITE | rex::X_PAGE_NOCACHE, 0, 0xFFFFFFFFu, 0x20) != 0);

  std::vector<rex::LogEntry> entries;
  sink->CopyEntries(entries);
  rex::RemoveSink(rex::log::krnl(), sink);
  REXCVAR_SET(log_noisy, old_noisy);

  const auto result_log_count =
      std::count_if(entries.begin(), entries.end(), [](const rex::LogEntry& entry) {
        return entry.level == spdlog::level::trace &&
               IsRoundedPhysicalAllocationResultLog(entry.text);
      });
  CHECK(result_log_count == 1);

  std::filesystem::remove_all(root, cleanup_error);
}
