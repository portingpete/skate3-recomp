#include <catch2/catch_test_macros.hpp>

#include <rex/system/xtypes.h>

using rex::X_HRESULT;

namespace rex::kernel::xam::apps {
rex::X_HRESULT HandleUnsupportedXmpCaptureOutput();
}  // namespace rex::kernel::xam::apps

TEST_CASE("Unsupported XMP capture output returns failure without trapping",
          "[kernel][xam_xmp]") {
  CHECK(rex::kernel::xam::apps::HandleUnsupportedXmpCaptureOutput() == X_E_FAIL);
}
