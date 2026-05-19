#pragma once

namespace rex::ui::d3d12 {

constexpr const char* MissingDxcompilerMessage() {
  return "Failed to load dxcompiler.dll, converted DXIL disassembly for debugging will be "
         "unavailable - if needed, download the DirectX Shader Compiler from "
         "https://github.com/microsoft/DirectXShaderCompiler/releases and place the DLL next to "
         "this executable or in the DLL search path";
}

}  // namespace rex::ui::d3d12
