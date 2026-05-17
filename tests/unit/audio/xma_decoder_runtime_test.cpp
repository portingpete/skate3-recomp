#include <algorithm>
#include <memory>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <rex/audio/xma/decoder.h>
#include <rex/logging.h>
#include <rex/logging/sink.h>
#include <rex/memory.h>
#include <rex/system/export_resolver.h>
#include <rex/system/function_dispatcher.h>

namespace {

struct TestAudioRuntime {
  TestAudioRuntime() : dispatcher(&memory, &export_resolver) {}

  rex::memory::Memory memory;
  rex::runtime::ExportResolver export_resolver;
  rex::runtime::FunctionDispatcher dispatcher;
};

bool IsXma0601WriteLog(std::string_view text) {
  return text.find("XMA: Write") != std::string_view::npos &&
         text.find("0601") != std::string_view::npos;
}

}  // namespace

TEST_CASE("XMA register 0601 writes are stored no-op commands", "[runtime][audio][xma]") {
  rex::InitLogging(nullptr, spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::apu(), spdlog::level::trace);

  TestAudioRuntime runtime;
  REQUIRE(runtime.memory.Initialize());

  auto decoder = std::make_unique<rex::audio::XmaDecoder>(&runtime.dispatcher);

  auto sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::apu(), sink);

  REXAPU_DEBUG("XMA 0601 capture sentinel");

  constexpr uint32_t kRegister0601Address = 0x7FEA1804;
  decoder->WriteRegister(kRegister0601Address, 0x02000000);
  decoder->WriteRegister(kRegister0601Address, 0x03000000);

  std::vector<rex::LogEntry> entries;
  sink->CopyEntries(entries);
  rex::RemoveSink(rex::log::apu(), sink);

  CHECK(std::any_of(entries.begin(), entries.end(), [](const rex::LogEntry& entry) {
    return entry.text.find("XMA 0601 capture sentinel") != std::string::npos;
  }));

  CHECK(std::count_if(entries.begin(), entries.end(), [](const rex::LogEntry& entry) {
          return entry.level >= spdlog::level::debug && IsXma0601WriteLog(entry.text);
        }) == 0);
}
