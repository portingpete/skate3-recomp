#include <catch2/catch_test_macros.hpp>

#include <rex/kernel/init.h>
#include <rex/kernel/xam/module.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/types.h>

namespace rex::kernel::xam {
void XamLoaderLaunchTitle_entry(mapped_string raw_name_ptr, u32 flags);
}  // namespace rex::kernel::xam

TEST_CASE("XamLoaderLaunchTitle accepts dashboard exit requests", "[kernel][xam][loader]") {
  rex::Runtime runtime({}, {}, {}, {});
  rex::RuntimeConfig config;
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == 0);

  auto xam = runtime.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  REQUIRE(xam);
  xam->loader_data().launch_path = "game:\\stale.xex";

  rex::kernel::xam::XamLoaderLaunchTitle_entry(nullptr, 0x12345678);

  CHECK(xam->loader_data().launch_flags == 0x12345678);
  CHECK(xam->loader_data().launch_path.empty());
}

TEST_CASE("XamLoaderLaunchTitle empty path relaunches the default title",
          "[kernel][xam][loader]") {
  rex::Runtime runtime({}, {}, {}, {});
  rex::RuntimeConfig config;
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == 0);

  auto xam = runtime.kernel_state()->GetKernelModule<rex::kernel::xam::XamModule>("xam.xex");
  REQUIRE(xam);

  char launch_path[] = "";
  rex::kernel::xam::XamLoaderLaunchTitle_entry(mapped_string(launch_path, 0x1000), 0x20);

  CHECK(xam->loader_data().launch_flags == 0x20);
  CHECK(xam->loader_data().launch_path == "game:\\default.xex");
}
