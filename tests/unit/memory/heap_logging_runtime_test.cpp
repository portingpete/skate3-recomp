#include <algorithm>
#include <memory>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <rex/logging.h>
#include <rex/logging/sink.h>
#include <rex/memory.h>
#include <rex/system/xmemory.h>

namespace {

rex::memory::BaseHeap* MutableHeap(const rex::memory::BaseHeap* heap) {
  return const_cast<rex::memory::BaseHeap*>(heap);
}

bool IsDuplicateReserveDetailLog(const rex::LogEntry& entry) {
  return entry.category == "sys" &&
         entry.text.find("BaseHeap::AllocFixed duplicate reserve rejected: requested_base=") !=
             std::string_view::npos;
}

bool IsDuplicateReserveSuppressionLog(const rex::LogEntry& entry) {
  return entry.category == "sys" &&
         entry.text.find("BaseHeap::AllocFixed duplicate reserve rejected: suppressing repeated") !=
             std::string_view::npos;
}

}  // namespace

TEST_CASE("AllocFixed duplicate reserve probe logs are coalesced", "[memory][heap][logging]") {
  rex::InitLogging(nullptr, spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::sys(), spdlog::level::trace);
  rex::SetAllLevels(spdlog::level::trace);

  rex::memory::Memory memory;
  REQUIRE(memory.Initialize());
  auto* heap = MutableHeap(memory.LookupHeap(0x60000000));
  REQUIRE(heap != nullptr);

  constexpr uint32_t kBase = 0x62000000;
  constexpr uint32_t kRequestSize = 0x00540000;
  constexpr uint32_t kCoveredSize = 0x00600000;
  constexpr uint32_t kStride = 0x00010000;

  REQUIRE(heap->AllocFixed(kBase, kCoveredSize, 0x10000, rex::memory::kMemoryAllocationReserve,
                           rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite));

  auto sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::sys(), sink);

  for (uint32_t i = 0; i < 12; ++i) {
    CHECK_FALSE(heap->AllocFixed(kBase + i * kStride, kRequestSize, 0x10000,
                                 rex::memory::kMemoryAllocationReserve,
                                 rex::memory::kMemoryProtectRead |
                                     rex::memory::kMemoryProtectWrite));
  }

  CHECK(heap->AllocFixed(kBase, kRequestSize, 0x10000, rex::memory::kMemoryAllocationCommit,
                         rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite));

  std::vector<rex::LogEntry> entries;
  sink->CopyEntries(entries);

  rex::RemoveSink(rex::log::sys(), sink);

  CHECK(std::count_if(entries.begin(), entries.end(), IsDuplicateReserveDetailLog) == 4);
  CHECK(std::count_if(entries.begin(), entries.end(), IsDuplicateReserveSuppressionLog) == 1);

  heap->Release(kBase, nullptr);
}