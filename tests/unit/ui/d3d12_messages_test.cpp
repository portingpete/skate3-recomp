#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "ui/d3d12/d3d12_messages.h"

TEST_CASE("D3D12 optional dxcompiler diagnostic uses RexGlue wording",
          "[ui][d3d12][diagnostics]") {
  const std::string_view message = rex::ui::d3d12::MissingDxcompilerMessage();

  CHECK(message.find("dxcompiler.dll") != std::string_view::npos);
  CHECK(message.find("converted DXIL disassembly for debugging will be unavailable") !=
        std::string_view::npos);
  CHECK(message.find("DirectX Shader Compiler") != std::string_view::npos);
  CHECK(message.find("this executable or in the DLL search path") != std::string_view::npos);
  CHECK(message.find("Xenia") == std::string_view::npos);
}
