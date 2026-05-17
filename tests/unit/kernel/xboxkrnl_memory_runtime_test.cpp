#include <algorithm>
#include <memory>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <rex/logging.h>
#include <rex/logging/sink.h>
#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::kernel::xboxkrnl {
u32 NtAllocateVirtualMemory_entry(mapped_u32 base_addr_ptr, mapped_u32 region_size_ptr,
                                  u32 alloc_type, u32 protect_bits, u32 debug_memory);
}  // namespace rex::kernel::xboxkrnl

namespace {
bool IsDevkitMemoryHintLog(std::string_view text) {
  return text.find("devkit memory") != std::string_view::npos &&
         text.find("debug_memory=") != std::string_view::npos;
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
