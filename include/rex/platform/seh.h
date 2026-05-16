/**
 * @file        platform/seh.h
 * @brief       Platform-specific SEH implementation details
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license     BSD 3-Clause License
 */

#pragma once

#include <atomic>
#include <cstdint>

#include <rex/types.h>

namespace rex {
class SehException;
}

namespace rex::platform {

inline std::atomic<bool> g_seh_initialized{false};

/// Thread-local SEH state for capturing exception info.
struct SehThreadState {
  u32 code = 0;
  uintptr_t info[2] = {0, 0};
  bool raised_by_runtime = false;
};

/// Get the thread-local SEH state.
SehThreadState& seh_thread_state();

/// SEH filter function - captures exception info and determines whether to handle.
/// Returns non-zero if the exception should be handled.
int seh_filter(u32 code, void* exception_pointers);

/// SEH filter for guest thread boundaries. It only handles exceptions raised
/// through the runtime SEH path, so native host faults still continue search.
int seh_thread_boundary_filter(u32 code, void* exception_pointers);

/// Re-raise the captured exception.
[[noreturn]] void seh_rethrow();

/// Raise a guest exception so generated SEH wrappers can dispatch it.
[[noreturn]] void seh_raise(u32 code, uintptr_t info0 = 0, uintptr_t info1 = 0,
                            u32 info_count = 0);

/// Initialize SEH signal handlers (POSIX only, no-op on Windows).
void seh_initialize();

/// Thread-local flag for POSIX: true when in SEH-protected code.
bool& seh_active();

}  // namespace rex::platform
