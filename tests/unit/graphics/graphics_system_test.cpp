#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <rex/graphics/graphics_system.h>
#include <rex/logging.h>
#include <rex/logging/sink.h>

namespace {

class TestGraphicsSystem : public rex::graphics::GraphicsSystem {
 public:
  std::string name() const override { return "test_graphics"; }

  void WriteGuestRegisterForTest(uint32_t register_index, uint32_t value) {
    WriteRegister(register_index * 4, value);
  }

 protected:
  void CreateProvider([[maybe_unused]] bool with_presentation) override {}

  std::unique_ptr<rex::graphics::CommandProcessor> CreateCommandProcessor() override {
    return nullptr;
  }
};

uint32_t FindUnknownRegisterIndex() {
  for (uint32_t i = 0; i < rex::graphics::RegisterFile::kRegisterCount; ++i) {
    if (!rex::graphics::RegisterFile::GetRegisterInfo(i) && i != 0x01C5 && i != 0x1844) {
      return i;
    }
  }
  return rex::graphics::RegisterFile::kRegisterCount;
}

bool IsUnknownGpuRegisterWarning(const rex::LogEntry& entry, std::string_view register_name) {
  const auto needle = fmt::format("Unknown GPU register {} write", register_name);
  return entry.level >= spdlog::level::warn && entry.text.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("Known MMIO GPU register writes are stored without unknown-register warnings",
          "[graphics][graphics_system][logging]") {
  rex::InitLogging(nullptr, spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::gpu(), spdlog::level::trace);

  constexpr uint32_t kKnownDebugRegister = 0x0069;  // DBG_CNTL1_REG.
  REQUIRE(rex::graphics::RegisterFile::GetRegisterInfo(kKnownDebugRegister) != nullptr);

  const uint32_t unknown_register = FindUnknownRegisterIndex();
  REQUIRE(unknown_register < rex::graphics::RegisterFile::kRegisterCount);

  auto sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(sink);

  TestGraphicsSystem graphics;
  graphics.WriteGuestRegisterForTest(kKnownDebugRegister, 0x00003333);
  graphics.WriteGuestRegisterForTest(unknown_register, 0x12345678);

  std::vector<rex::LogEntry> entries;
  sink->CopyEntries(entries);
  rex::RemoveSink(sink);

  CHECK(graphics.register_file()->values[kKnownDebugRegister] == 0x00003333);
  CHECK(graphics.register_file()->values[unknown_register] == 0x12345678);

  CHECK(std::none_of(entries.begin(), entries.end(), [](const rex::LogEntry& entry) {
    return IsUnknownGpuRegisterWarning(entry, "0069");
  }));
  CHECK(
      std::count_if(entries.begin(), entries.end(), [unknown_register](const rex::LogEntry& entry) {
        return IsUnknownGpuRegisterWarning(entry, fmt::format("{:04X}", unknown_register));
      }) == 1);
}
