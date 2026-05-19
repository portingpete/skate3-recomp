#pragma once

namespace rex::graphics {

constexpr const char* InvalidFetchConstantBypassHint() {
  return "This is incorrect behavior; to bypass it, rerun this executable with "
         "--gpu-allow-invalid-fetch-constants=true or set "
         "gpu_allow_invalid_fetch_constants=true.";
}

}  // namespace rex::graphics
