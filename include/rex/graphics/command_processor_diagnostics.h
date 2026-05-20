#pragma once

#include <cstdint>
#include <string>

#include <fmt/format.h>

namespace rex::graphics {

inline std::string BuildIndirectRingBufferPacketFailureMessage(uint32_t base_ptr,
                                                               uint32_t dword_count,
                                                               uint32_t read_offset,
                                                               uint32_t remaining_bytes,
                                                               bool shutdown_requested) {
  return fmt::format(
      "**** INDIRECT RINGBUFFER: Failed to execute packet "
      "(base=0x{:08X}, dwords={}, read_offset=0x{:08X}, remaining={} bytes, "
      "shutdown_requested={}).",
      base_ptr, dword_count, read_offset, remaining_bytes, shutdown_requested);
}

}  // namespace rex::graphics
