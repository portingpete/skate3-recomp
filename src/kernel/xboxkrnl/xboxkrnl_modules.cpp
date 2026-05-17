/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/kernel/xboxkrnl/private.h>
#include <rex/logging.h>
#include <rex/hook.h>
#include <rex/thread/mutex.h>
#include <rex/types.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xexception.h>
#include <rex/system/xthread.h>
#include <rex/system/xtypes.h>
#include <rex/system/lzx.h>

#include <algorithm>
#include <bit>
#include <mutex>
#include <unordered_map>

namespace rex::kernel::xboxkrnl {
using namespace rex::system;

namespace {

constexpr uint32_t kLdiMinWindowSize = 0x8000;
constexpr uint32_t kLdiMaxWindowSize = 0x200000;
constexpr uint32_t kLdiMinWorkspaceSize = 0x9800;
constexpr uint32_t kLdiHandleBase = 0x4C444900;  // "LDI\0"
constexpr uint32_t kLdiHandleMask = 0x0000FFFF;

struct LdiDecompressionContext {
  uint32_t window_size;
};

std::mutex& LdiContextMutex() {
  static std::mutex mutex;
  return mutex;
}

std::unordered_map<uint32_t, LdiDecompressionContext>& LdiContexts() {
  static std::unordered_map<uint32_t, LdiDecompressionContext> contexts;
  return contexts;
}

uint32_t& NextLdiHandleIndex() {
  static uint32_t next_handle_index = 1;
  return next_handle_index;
}

uint32_t NormalizeLdiWindowSize(uint32_t requested_size) {
  uint32_t window_size = std::max(requested_size, kLdiMinWindowSize);
  if (window_size > kLdiMaxWindowSize) {
    return window_size;
  }
  if (!std::has_single_bit(window_size)) {
    window_size = std::bit_ceil(window_size);
  }
  return window_size;
}

uint32_t AllocateLdiHandle(uint32_t window_size) {
  std::lock_guard lock(LdiContextMutex());
  auto& contexts = LdiContexts();
  uint32_t& next_handle_index = NextLdiHandleIndex();

  for (uint32_t attempt = 0; attempt < kLdiHandleMask; ++attempt) {
    const uint32_t index = next_handle_index;
    next_handle_index = (next_handle_index % kLdiHandleMask) + 1;

    const uint32_t handle = kLdiHandleBase | index;
    if (!contexts.contains(handle)) {
      contexts.emplace(handle, LdiDecompressionContext{window_size});
      return handle;
    }
  }

  return 0;
}

bool LookupLdiContext(uint32_t handle, LdiDecompressionContext* out_context) {
  std::lock_guard lock(LdiContextMutex());
  auto& contexts = LdiContexts();
  auto it = contexts.find(handle);
  if (it == contexts.end()) {
    return false;
  }
  *out_context = it->second;
  return true;
}

}  // namespace

u32 XexCheckExecutablePrivilege_entry(u32 privilege) {
  REXKRNL_IMPORT_TRACE("XexCheckExecutablePrivilege", "priv={}", (uint32_t)privilege);
  // BOOL
  // DWORD Privilege

  // Privilege is bit position in xe_xex2_system_flags enum - so:
  // Privilege=6 -> 0x00000040 -> XEX_SYSTEM_INSECURE_SOCKETS
  uint32_t mask = 1 << privilege;

  auto module = REX_KERNEL_STATE()->GetExecutableModule();
  if (!module) {
    return 0;
  }

  uint32_t flags = 0;
  module->GetOptHeader<uint32_t>(XEX_HEADER_SYSTEM_FLAGS, &flags);

  return (flags & mask) > 0;
}

u32 XexGetModuleHandle_entry(mapped_string module_name, mapped_u32 hmodule_ptr) {
  object_ref<XModule> module;

  if (!module_name) {
    module = REX_KERNEL_STATE()->GetExecutableModule();
  } else {
    module = REX_KERNEL_STATE()->GetModule(module_name.value());
  }

  if (!module) {
    *hmodule_ptr = 0;
    return X_ERROR_NOT_FOUND;
  }

  // NOTE: we don't retain the handle for return.
  *hmodule_ptr = module->hmodule_ptr();

  return X_ERROR_SUCCESS;
}

u32 XexGetModuleSection_entry(mapped_void hmodule, mapped_string name, mapped_u32 data_ptr,
                              mapped_u32 size_ptr) {
  X_STATUS result = X_STATUS_SUCCESS;

  auto module = XModule::GetFromHModule(REX_KERNEL_STATE(), hmodule);
  if (module) {
    uint32_t section_data = 0;
    uint32_t section_size = 0;
    result = module->GetSection(name.value(), &section_data, &section_size);
    if (XSUCCEEDED(result)) {
      *data_ptr = section_data;
      *size_ptr = section_size;
    }
  } else {
    result = X_STATUS_INVALID_HANDLE;
  }

  return result;
}

u32 XexLoadImage_entry(mapped_string module_name, u32 module_flags, u32 min_version,
                       mapped_u32 hmodule_ptr) {
  (void)module_flags;
  (void)min_version;

  X_STATUS result = X_STATUS_NO_SUCH_FILE;

  uint32_t hmodule = 0;
  {
    // Lookup + load_count++ must be atomic vs XexUnloadImage to prevent
    // resurrecting a module between the read of hmodule and the increment.
    // The fresh-load path can't share this lock: LoadUserModule runs
    // DllMain ATTACH outside the global lock by design.
    auto lock = rex::thread::global_critical_region::AcquireDirect();
    auto module = REX_KERNEL_STATE()->GetModule(module_name.value());
    if (module) {
      hmodule = module->hmodule_ptr();
      auto ldr_data = REX_KERNEL_MEMORY()->TranslateVirtual<X_LDR_DATA_TABLE_ENTRY*>(hmodule);
      ldr_data->load_count++;
      result = X_STATUS_SUCCESS;
    }
  }

  if (!hmodule) {
    auto user_module = REX_KERNEL_STATE()->LoadUserModule(module_name.value());
    if (user_module) {
      // Released by the last XexUnloadImage call.
      auto user_module_raw = user_module.release();
      hmodule = user_module_raw->hmodule_ptr();
      auto lock = rex::thread::global_critical_region::AcquireDirect();
      auto ldr_data = REX_KERNEL_MEMORY()->TranslateVirtual<X_LDR_DATA_TABLE_ENTRY*>(hmodule);
      ldr_data->load_count++;
      result = X_STATUS_SUCCESS;
    }
  }

  *hmodule_ptr = hmodule;

  return result;
}

u32 XexUnloadImage_entry(mapped_void hmodule) {
  auto module = XModule::GetFromHModule(REX_KERNEL_STATE(), hmodule);
  if (!module) {
    return X_STATUS_INVALID_HANDLE;
  }
  if (module->module_type() == XModule::ModuleType::kKernelModule) {
    return X_STATUS_SUCCESS;
  }

  // Decrement-and-check under the global lock so concurrent unloads can't both
  // observe zero and double-free.
  bool last_ref;
  {
    auto lock = rex::thread::global_critical_region::AcquireDirect();
    auto ldr_data = hmodule.as<X_LDR_DATA_TABLE_ENTRY*>();
    last_ref = (--ldr_data->load_count == 0);
  }

  if (last_ref) {
    module->Release();
    REX_KERNEL_STATE()->UnloadUserModule(
        object_ref<UserModule>(reinterpret_cast<UserModule*>(module.release())));
  }

  return X_STATUS_SUCCESS;
}

u32 XexUnloadImageAndExitThread_entry(mapped_void hmodule, u32 exit_code) {
  if (!XThread::IsInThread()) {
    return X_STATUS_INVALID_HANDLE;
  }

  XThread* thread = XThread::GetCurrentThread();
  (void)XexUnloadImage_entry(hmodule);
  return thread->Exit(exit_code);
}

u32 XexGetProcedureAddress_entry(mapped_void hmodule, u32 ordinal, mapped_u32 out_function_ptr) {
  // May be entry point?
  assert_not_zero(ordinal);

  uint32_t caller_address = 0;
  auto* thread = XThread::GetCurrentThread();
  if (thread && thread->thread_state() && thread->thread_state()->context()) {
    caller_address = static_cast<uint32_t>(thread->thread_state()->context()->lr);
  }

  bool is_string_name = (ordinal & 0xFFFF0000) != 0;
  auto string_name = reinterpret_cast<const char*>(REX_KERNEL_MEMORY()->TranslateVirtual(ordinal));

  X_STATUS result = X_STATUS_INVALID_HANDLE;

  object_ref<XModule> module;
  if (!hmodule) {
    module = REX_KERNEL_STATE()->GetExecutableModule();
  } else {
    module = XModule::GetFromHModule(REX_KERNEL_STATE(), hmodule);
  }
  if (module) {
    uint32_t ptr;
    if (is_string_name) {
      ptr = module->GetProcAddressByName(string_name);
    } else {
      ptr = module->GetProcAddressByOrdinal(ordinal, caller_address);
    }
    if (ptr) {
      *out_function_ptr = ptr;
      result = X_STATUS_SUCCESS;
    } else {
      if (is_string_name) {
        REXKRNL_WARN("ERROR: XexGetProcedureAddress export '{}' in '{}' not found!", string_name,
                     module->name());
      } else {
        REXKRNL_WARN(
            "ERROR: XexGetProcedureAddress ordinal {} (0x{:X}) in '{}' not "
            "found!",
            ordinal, ordinal, module->name());
      }
      *out_function_ptr = 0;
      result = X_STATUS_DRIVER_ENTRYPOINT_NOT_FOUND;
    }
  }

  return result;
}

void ExRegisterTitleTerminateNotification_entry(ppc_ptr_t<X_EX_TITLE_TERMINATE_REGISTRATION> reg,
                                                u32 create) {
  if (create) {
    // Adding.
    REX_KERNEL_STATE()->RegisterTitleTerminateNotification(reg->notification_routine,
                                                           reg->priority);
  } else {
    // Removing.
    REX_KERNEL_STATE()->RemoveTitleTerminateNotification(reg->notification_routine);
  }
}

u32 XexLoadImageHeaders_entry(mapped_string path, mapped_void headers) {
  (void)headers;
  REXKRNL_DEBUG("XexLoadImageHeaders({}) - stub", path.value());
  return X_STATUS_NOT_IMPLEMENTED;
}

u32 LDICreateDecompression_entry(mapped_u32 window_size_ptr, mapped_u32 chunk_size_ptr,
                                 mapped_void alloc_proc, mapped_void free_proc, u32 memory_size,
                                 mapped_u32 workspace_size_ptr, mapped_u32 context_out_ptr) {
  (void)alloc_proc;
  (void)free_proc;

  if (!window_size_ptr || !chunk_size_ptr || !workspace_size_ptr || !context_out_ptr) {
    return X_STATUS_INVALID_PARAMETER;
  }

  const uint32_t window_size = NormalizeLdiWindowSize(window_size_ptr.value());
  if (window_size > kLdiMaxWindowSize) {
    *context_out_ptr = 0;
    return X_STATUS_INVALID_PARAMETER;
  }

  if (memory_size && memory_size < kLdiMinWorkspaceSize) {
    *context_out_ptr = 0;
    return X_STATUS_NO_MEMORY;
  }

  const uint32_t handle = AllocateLdiHandle(window_size);
  if (!handle) {
    *context_out_ptr = 0;
    return X_STATUS_NO_MEMORY;
  }

  *window_size_ptr = window_size;
  *chunk_size_ptr = std::max<uint32_t>(chunk_size_ptr.value(), kLdiMinWindowSize);
  *workspace_size_ptr = std::max<uint32_t>(workspace_size_ptr.value(), kLdiMinWorkspaceSize);
  *context_out_ptr = handle;
  return X_STATUS_SUCCESS;
}

u32 LDIDecompress_entry(u32 handle, mapped_void input, u32 input_size, mapped_void output,
                        mapped_u32 output_size_ptr) {
  if (!handle || !input || !output || !output_size_ptr || input_size == 0) {
    return X_STATUS_INVALID_PARAMETER;
  }

  LdiDecompressionContext context{};
  if (!LookupLdiContext(handle, &context)) {
    return X_STATUS_INVALID_HANDLE;
  }

  const uint32_t output_size = output_size_ptr.value();
  if (!output_size) {
    return X_STATUS_INVALID_PARAMETER;
  }

  const int result = lzx_decompress(input.host_address(), input_size, output.host_address(),
                                    output_size, context.window_size, nullptr, 0);
  if (result != 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  *output_size_ptr = output_size;
  return X_STATUS_SUCCESS;
}

u32 LDIResetDecompression_entry(u32 handle) {
  LdiDecompressionContext context{};
  return LookupLdiContext(handle, &context) ? X_STATUS_SUCCESS : X_STATUS_INVALID_HANDLE;
}

u32 LDIDestroyDecompression_entry(u32 handle) {
  std::lock_guard lock(LdiContextMutex());
  auto& contexts = LdiContexts();
  const auto erased = contexts.erase(handle);
  return erased ? X_STATUS_SUCCESS : X_STATUS_INVALID_HANDLE;
}

}  // namespace rex::kernel::xboxkrnl

REX_EXPORT(__imp__XexCheckExecutablePrivilege,
           rex::kernel::xboxkrnl::XexCheckExecutablePrivilege_entry)
REX_EXPORT(__imp__XexGetModuleHandle, rex::kernel::xboxkrnl::XexGetModuleHandle_entry)
REX_EXPORT(__imp__XexGetModuleSection, rex::kernel::xboxkrnl::XexGetModuleSection_entry)
REX_EXPORT(__imp__XexLoadImage, rex::kernel::xboxkrnl::XexLoadImage_entry)
REX_EXPORT(__imp__XexUnloadImage, rex::kernel::xboxkrnl::XexUnloadImage_entry)
REX_EXPORT(__imp__XexUnloadImageAndExitThread,
           rex::kernel::xboxkrnl::XexUnloadImageAndExitThread_entry)
REX_EXPORT(__imp__XexGetProcedureAddress, rex::kernel::xboxkrnl::XexGetProcedureAddress_entry)
REX_EXPORT(__imp__ExRegisterTitleTerminateNotification,
           rex::kernel::xboxkrnl::ExRegisterTitleTerminateNotification_entry)
REX_EXPORT(__imp__XexLoadImageHeaders, rex::kernel::xboxkrnl::XexLoadImageHeaders_entry)

REX_EXPORT_STUB(__imp__XexLoadExecutable);
REX_EXPORT_STUB(__imp__XexLoadImageFromMemory);
REX_EXPORT_STUB(__imp__XexPcToFileHeader);
REX_EXPORT_STUB(__imp__XexRegisterPatchDescriptor);
REX_EXPORT_STUB(__imp__XexSendDeferredNotifications);
REX_EXPORT_STUB(__imp__XexStartExecutable);
REX_EXPORT_STUB(__imp__XexUnloadTitleModules);
REX_EXPORT_STUB(__imp__XexVerifyImageHeaders);
REX_EXPORT_STUB(__imp__XexGetModuleImportVersions);
REX_EXPORT_STUB(__imp__XexActivationGetNonce);
REX_EXPORT_STUB(__imp__XexActivationSetLicense);
REX_EXPORT_STUB(__imp__XexActivationVerifyOwnership);
REX_EXPORT_STUB(__imp__XexDisableVerboseDbgPrint);
REX_EXPORT_STUB(__imp__XexImportTraceEnable);
REX_EXPORT_STUB(__imp__XexSetExecutablePrivilege);
REX_EXPORT_STUB(__imp__XexSetLastKdcTime);
REX_EXPORT_STUB(__imp__XexTransformImageKey);
REX_EXPORT_STUB(__imp__XexShimDisable);
REX_EXPORT_STUB(__imp__XexShimEnable);
REX_EXPORT_STUB(__imp__XexShimEntryDisable);
REX_EXPORT_STUB(__imp__XexShimEntryEnable);
REX_EXPORT_STUB(__imp__XexShimEntryRegister);
REX_EXPORT_STUB(__imp__XexShimLock);
REX_EXPORT_STUB(__imp__XexTitleHash);
REX_EXPORT_STUB(__imp__XexTitleHashClose);
REX_EXPORT_STUB(__imp__XexTitleHashContinue);
REX_EXPORT_STUB(__imp__XexTitleHashOpen);
REX_EXPORT_STUB(__imp__XexReserveCodeBuffer);
REX_EXPORT_STUB(__imp__XexCommitCodeBuffer);
REX_EXPORT_STUB(__imp__XexRegisterUsermodeModule);
REX_EXPORT(__imp__LDICreateDecompression, rex::kernel::xboxkrnl::LDICreateDecompression_entry)
REX_EXPORT(__imp__LDIDecompress, rex::kernel::xboxkrnl::LDIDecompress_entry)
REX_EXPORT(__imp__LDIDestroyDecompression, rex::kernel::xboxkrnl::LDIDestroyDecompression_entry)
REX_EXPORT(__imp__LDIResetDecompression, rex::kernel::xboxkrnl::LDIResetDecompression_entry)
