#include <string>

#include <catch2/catch_test_macros.hpp>

#include <rex/graphics/command_processor_diagnostics.h>

TEST_CASE("Indirect ringbuffer packet failure includes execution context",
          "[graphics][command_processor][diagnostics]") {
  const std::string shutdown_message =
      rex::graphics::BuildIndirectRingBufferPacketFailureMessage(0x1A2B3C40, 32, 0x0C, 116, true);

  CHECK(shutdown_message ==
        "**** INDIRECT RINGBUFFER: Failed to execute packet "
        "(base=0x1A2B3C40, dwords=32, read_offset=0x0000000C, remaining=116 bytes, "
        "shutdown_requested=true).");

  const std::string active_message =
      rex::graphics::BuildIndirectRingBufferPacketFailureMessage(0x80001000, 2, 0x04, 4, false);

  CHECK(active_message.find("base=0x80001000") != std::string::npos);
  CHECK(active_message.find("dwords=2") != std::string::npos);
  CHECK(active_message.find("shutdown_requested=false") != std::string::npos);
}
