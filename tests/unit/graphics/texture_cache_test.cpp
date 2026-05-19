#include <algorithm>
#include <memory>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <rex/cvar.h>
#include <rex/graphics/flags.h>
#include <rex/graphics/pipeline/texture/cache.h>
#include <rex/graphics/xenos.h>
#include <rex/logging.h>
#include <rex/logging/sink.h>

namespace {

struct TextureCacheBindingTestAccess : rex::graphics::TextureCache {
  using TextureCache::BindingInfoFromFetchConstant;
  using TextureCache::TextureKey;
};

bool IsInvalidTextureFetchWarning(std::string_view text) {
  return text.find("Texture fetch constant") != std::string_view::npos &&
         text.find("has \"invalid\" type") != std::string_view::npos;
}

rex::graphics::xenos::xe_gpu_texture_fetch_t MakeInvalidTextureFetch(uint32_t marker_dword) {
  rex::graphics::xenos::xe_gpu_texture_fetch_t fetch{};
  fetch.type = rex::graphics::xenos::FetchConstantType::kInvalidTexture;
  fetch.dword_3 = marker_dword;
  return fetch;
}

}  // namespace

TEST_CASE("Invalid texture fetch warnings are emitted once per constant",
          "[graphics][texture][logging]") {
  rex::InitLogging(nullptr, spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::gpu(), spdlog::level::trace);
  REQUIRE(rex::cvar::SetFlagByName("gpu_allow_invalid_fetch_constants", "false"));

  auto sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(sink);

  TextureCacheBindingTestAccess::TextureKey key;
  uint8_t swizzled_signs = 0;
  TextureCacheBindingTestAccess::BindingInfoFromFetchConstant(MakeInvalidTextureFetch(0x01000000),
                                                              key, &swizzled_signs);
  TextureCacheBindingTestAccess::BindingInfoFromFetchConstant(MakeInvalidTextureFetch(0x01000000),
                                                              key, &swizzled_signs);
  TextureCacheBindingTestAccess::BindingInfoFromFetchConstant(MakeInvalidTextureFetch(0x02000000),
                                                              key, &swizzled_signs);

  std::vector<rex::LogEntry> entries;
  sink->CopyEntries(entries);
  rex::RemoveSink(sink);

  const auto warning_count =
      std::count_if(entries.begin(), entries.end(), [](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn && IsInvalidTextureFetchWarning(entry.text);
      });
  const auto repeated_debug_count =
      std::count_if(entries.begin(), entries.end(), [](const rex::LogEntry& entry) {
        return entry.level == spdlog::level::debug &&
               entry.text.find("has repeated \"invalid\" type") != std::string_view::npos;
      });

  CHECK(warning_count == 2);
  CHECK(repeated_debug_count == 0);
}
