/**
 * @file        memory.h
 * @brief       Memory subsystem umbrella header
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#pragma once

// Memory utilities (page allocation, protection, load/store helpers)
#include <rex/memory/utils.h>

// Arena allocator
#include <rex/memory/arena.h>

// Memory-mapped files
#include <rex/memory/mapped_memory.h>

// Ring buffer
#include <rex/memory/ring_buffer.h>

// Memory class (Xbox 360 guest memory system with heaps)
#include <rex/system/xmemory.h>

namespace rex::ppc {
namespace detail {

inline std::intptr_t HostAddressOffset(uint32_t guest_address) {
#if REX_PLATFORM_WIN32
  return static_cast<std::intptr_t>(static_cast<uint64_t>(guest_address) +
                                    (guest_address >= 0xE0000000u ? 0x1000u : 0u));
#else
  return static_cast<std::intptr_t>(guest_address);
#endif
}

inline bool HostRangeAccessible(uint8_t* host_address, size_t byte_count, bool write_required) {
  size_t region_len = byte_count;
  rex::memory::PageAccess access{};
  if (!rex::memory::QueryProtect(host_address, region_len, access)) {
    return false;
  }
  if (write_required) {
    return access == rex::memory::PageAccess::kReadWrite ||
           access == rex::memory::PageAccess::kExecuteReadWrite;
  }
  return access == rex::memory::PageAccess::kReadOnly ||
         access == rex::memory::PageAccess::kReadWrite ||
         access == rex::memory::PageAccess::kExecuteReadOnly ||
         access == rex::memory::PageAccess::kExecuteReadWrite;
}

}  // namespace detail

inline bool TryStoreU32NoFault(uint8_t* base, uint32_t addr, uint32_t value) {
  auto* host_ptr = base + detail::HostAddressOffset(addr);
#if REX_PLATFORM_WIN32
  __try {
    *(volatile uint32_t*)host_ptr = rex::byte_swap(value);
    return true;
  } __except (1) {
    return false;
  }
#else
  if (!detail::HostRangeAccessible(host_ptr, sizeof(uint32_t), true)) {
    return false;
  }
  *(volatile uint32_t*)host_ptr = rex::byte_swap(value);
  return true;
#endif
}

inline bool TryLoadU32NoFault(uint8_t* base, uint32_t addr, uint32_t* out_value) {
  auto* host_ptr = base + detail::HostAddressOffset(addr);
  if (!detail::HostRangeAccessible(host_ptr, sizeof(uint32_t), false)) {
    *out_value = 0;
    return false;
  }
#if REX_PLATFORM_WIN32
  __try {
    *out_value = rex::byte_swap(*(volatile uint32_t*)host_ptr);
    return true;
  } __except (1) {
    *out_value = 0;
    return false;
  }
#else
  *out_value = rex::byte_swap(*(volatile uint32_t*)host_ptr);
  return true;
#endif
}

}  // namespace rex::ppc
