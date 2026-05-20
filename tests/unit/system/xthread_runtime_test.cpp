#include <utility>

#include <catch2/catch_test_macros.hpp>

#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>

TEST_CASE("KernelState refreshes registered guest thread kernel time fields",
          "[runtime][system][xthread][time]") {
  rex::Runtime runtime({}, {}, {}, {});
  rex::RuntimeConfig config;
  config.tool_mode = true;
  REQUIRE(runtime.Setup(std::move(config)) == 0);

  auto thread = rex::system::make_object<rex::system::XThread>(
      runtime.kernel_state(), 16 * 1024, 0, 0, 0, rex::system::X_CREATE_SUSPENDED, true);
  REQUIRE(thread->Create() == 0);

  auto* kthread = thread->guest_object<rex::system::X_KTHREAD>();
  REQUIRE(kthread != nullptr);
  kthread->kernel_time = 0;

  runtime.kernel_state()->UpdateThreadKernelTimes(0x12345678u);

  CHECK(static_cast<uint32_t>(kthread->kernel_time) == 0x12345678u);
  REQUIRE(thread->Terminate(0) == 0);
}