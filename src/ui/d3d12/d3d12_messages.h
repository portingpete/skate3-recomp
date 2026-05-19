#pragma once

namespace rex::ui::d3d12 {

constexpr const char* MissingDxcompilerMessage() {
  return "Failed to load dxcompiler.dll, converted DXIL disassembly for debugging will be "
         "unavailable - if needed, download the DirectX Shader Compiler from "
         "https://github.com/microsoft/DirectXShaderCompiler/releases and place the DLL next to "
         "this executable or in the DLL search path";
}

constexpr const char* RealtimePriorityPrivilegeFallbackMessage() {
  return "Failed to enable SeIncreaseBasePriorityPrivilege for global realtime Direct3D 12 "
         "command queue priority, falling back to high priority; try launching this executable "
         "as administrator";
}

constexpr const char* RealtimePriorityQueueCreationFallbackMessage() {
  return "Failed to create a Direct3D 12 direct command queue with global realtime priority, "
         "falling back to high priority; try launching this executable as administrator";
}

}  // namespace rex::ui::d3d12
