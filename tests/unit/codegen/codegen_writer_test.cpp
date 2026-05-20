#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#define _ALLOW_KEYWORD_MACROS
#define private public
#include <rex/codegen/binary_view.h>
#undef private

#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/codegen_context.h>
#include <rex/codegen/codegen_writer.h>
#include <rex/codegen/config.h>

#include "codegen/codegen_flags.h"

namespace fs = std::filesystem;

namespace {

struct TempDir {
  fs::path path;

  TempDir() : path(fs::temp_directory_path() / "rex_codegen_writer_test") {
    fs::remove_all(path);
    fs::create_directories(path);
  }

  ~TempDir() { fs::remove_all(path); }
};

rex::codegen::BinaryView MakeBinaryView(uint32_t base, std::span<const uint8_t> bytes) {
  rex::codegen::BinaryView view;
  view.sectionNames_.push_back(".text");
  view.sectionData_.emplace_back(bytes.begin(), bytes.end());
  view.sections_.push_back({
      .name = view.sectionNames_.back(),
      .baseAddress = base,
      .size = static_cast<uint32_t>(bytes.size()),
      .data = view.sectionData_.back().data(),
      .executable = true,
  });
  return view;
}

std::string ReadText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("CodegenWriter gives oversized generated functions self-describing files",
          "[codegen][CodegenWriter]") {
  constexpr uint32_t kFunctionBase = 0x1000;
  constexpr uint32_t kInstructionCount = 16384;

  std::vector<uint8_t> bytes(kInstructionCount * 4);
  for (size_t offset = 0; offset < bytes.size(); offset += 4) {
    bytes[offset + 0] = 0x60;
    bytes[offset + 1] = 0x00;
    bytes[offset + 2] = 0x00;
    bytes[offset + 3] = 0x00;
  }
  bytes[bytes.size() - 4] = 0x4E;
  bytes[bytes.size() - 3] = 0x80;
  bytes[bytes.size() - 2] = 0x00;
  bytes[bytes.size() - 1] = 0x20;

  TempDir tmp;
  {
    std::ofstream(tmp.path / "dummy.xex", std::ios::binary).put('\0');
  }

  rex::codegen::RecompilerConfig config;
  config.projectName = "writer_test";
  config.filePath = "dummy.xex";
  config.outDirectoryPath = "generated";
  config.generateExceptionHandlers = false;

  auto ctx = rex::codegen::CodegenContext::Create(MakeBinaryView(kFunctionBase, bytes), config);
  ctx.setConfigDir(tmp.path);
  ctx.analysisState().entryPoint = kFunctionBase;

  auto* node = ctx.graph.addFunction(kFunctionBase, static_cast<uint32_t>(bytes.size()),
                                     rex::codegen::FunctionAuthority::CONFIG, true);
  REQUIRE(node != nullptr);
  node->discover({rex::codegen::Block{.base = kFunctionBase,
                                      .size = static_cast<uint32_t>(bytes.size())}},
                 {}, {kFunctionBase});
  node->seal();

  const uint32_t oldMaxFileSize = REXCVAR_GET(max_file_size_bytes);
  REXCVAR_SET(max_file_size_bytes, 65536u);

  rex::codegen::CodegenWriter writer(ctx);
  const bool wrote = writer.write(false);

  REXCVAR_SET(max_file_size_bytes, oldMaxFileSize);
  REQUIRE(wrote);

  const fs::path generated = tmp.path / "generated";
  const fs::path oversizedFile = generated / "writer_test_recomp.0_00001000.cpp";
  CHECK(fs::exists(oversizedFile));
  CHECK_FALSE(fs::exists(generated / "writer_test_recomp.0.cpp"));

  const std::string sources = ReadText(generated / "sources.cmake");
  CHECK(sources.find("writer_test_recomp.0_00001000.cpp") != std::string::npos);

  const auto& written = writer.writtenFiles();
  CHECK(std::find(written.begin(), written.end(), "writer_test_recomp.0_00001000.cpp") !=
        written.end());
}
