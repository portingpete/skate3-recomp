/**
 * @file        core/seh_win.cpp
 * @brief       Windows platform SEH implementations
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license     BSD 3-Clause License
 */

#include <rex/platform.h>
#include <rex/platform/seh.h>

static_assert(REX_PLATFORM_WIN32, "This file is Windows-only");

#include <algorithm>
#include <cstdlib>
#include <intrin.h>

#include "platform_win.h"

#include <rex/logging.h>

namespace rex::platform {

namespace {

bool IsSehTraceEnabled() {
  static const bool enabled = [] {
    char* value = nullptr;
    size_t value_size = 0;
    if (_dupenv_s(&value, &value_size, "REX_DIAG_SEH_TRACE") != 0) {
      return false;
    }
    const bool is_enabled = value && value[0] != '\0' && value[0] != '0';
    free(value);
    return is_enabled;
  }();
  return enabled;
}

void LogSehTrace(const char* stage, uint32_t code, uintptr_t info0, uintptr_t info1,
                 uintptr_t pc, bool force_handle) {
  if (!IsSehTraceEnabled()) {
    return;
  }

  HMODULE module = nullptr;
  uintptr_t module_base = 0;
  char module_path[MAX_PATH] = {};
  if (pc && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(pc), &module)) {
    module_base = reinterpret_cast<uintptr_t>(module);
    GetModuleFileNameA(module, module_path, DWORD(sizeof(module_path)));
  }

  REXLOG_WARN("SEH trace {} code=0x{:08X} info0=0x{:X} info1=0x{:X} pc=0x{:016X} "
              "module_offset=0x{:X} force={} module={}",
              stage, code, info0, info1, pc, module_base ? pc - module_base : 0,
              force_handle, module_path[0] ? module_path : "<unknown>");
}

}  // namespace

static thread_local SehThreadState tls_seh_state;
static thread_local bool tls_seh_active = false;
static thread_local bool tls_force_handle_next_exception = false;

SehThreadState& seh_thread_state() {
  return tls_seh_state;
}

int seh_filter(uint32_t code, void* ep) {
  auto* pointers = static_cast<EXCEPTION_POINTERS*>(ep);
  const bool force_handle = tls_force_handle_next_exception;
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
      break;
    default:
      if (!force_handle) {
        return EXCEPTION_CONTINUE_SEARCH;
      }
      break;
  }

  tls_force_handle_next_exception = false;
  tls_seh_state.code = code;
  tls_seh_state.info[0] = 0;
  tls_seh_state.info[1] = 0;
  tls_seh_state.raised_by_runtime = force_handle;
  if (pointers && pointers->ExceptionRecord) {
    const auto* record = pointers->ExceptionRecord;
    if (record->NumberParameters > 0) {
      tls_seh_state.info[0] = record->ExceptionInformation[0];
    }
    if (record->NumberParameters > 1) {
      tls_seh_state.info[1] = record->ExceptionInformation[1];
    }
  }
  uintptr_t pc = 0;
  if (pointers) {
    if (pointers->ContextRecord) {
      pc = static_cast<uintptr_t>(pointers->ContextRecord->Rip);
    } else if (pointers->ExceptionRecord) {
      pc = reinterpret_cast<uintptr_t>(pointers->ExceptionRecord->ExceptionAddress);
    }
  }
  LogSehTrace("filter", tls_seh_state.code, tls_seh_state.info[0], tls_seh_state.info[1], pc,
              force_handle);
  return EXCEPTION_EXECUTE_HANDLER;
}

int seh_thread_boundary_filter(uint32_t code, void* ep) {
  const int result = seh_filter(code, ep);
  if (result != EXCEPTION_EXECUTE_HANDLER) {
    return result;
  }
  return tls_seh_state.raised_by_runtime ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

[[noreturn]] void seh_raise(uint32_t code, uintptr_t info0, uintptr_t info1, uint32_t info_count) {
  tls_seh_state.code = code;
  tls_seh_state.info[0] = info0;
  tls_seh_state.info[1] = info1;
  tls_seh_state.raised_by_runtime = true;
  tls_force_handle_next_exception = true;

  ULONG_PTR info[2] = {static_cast<ULONG_PTR>(info0), static_cast<ULONG_PTR>(info1)};
  const ULONG arg_count = std::min<ULONG>(info_count, 2);
  LogSehTrace("raise", code, info0, info1, reinterpret_cast<uintptr_t>(_ReturnAddress()),
              tls_force_handle_next_exception);
  RaiseException(code, EXCEPTION_NONCONTINUABLE, arg_count, arg_count ? info : nullptr);

#ifdef __clang__
  __builtin_unreachable();
#else
  __assume(false);
#endif
}

[[noreturn]] void seh_rethrow() {
  LogSehTrace("rethrow", tls_seh_state.code, tls_seh_state.info[0], tls_seh_state.info[1],
              reinterpret_cast<uintptr_t>(_ReturnAddress()), false);
  seh_raise(tls_seh_state.code, tls_seh_state.info[0], tls_seh_state.info[1], 2);
}

void seh_initialize() {
  // Native SEH needs no signal handler setup, but mark as initialized
  g_seh_initialized.store(true, std::memory_order_relaxed);
}

bool& seh_active() {
  return tls_seh_active;
}

}  // namespace rex::platform
