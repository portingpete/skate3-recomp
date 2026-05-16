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

#include <rex/kernel/xam/private.h>
#include <rex/logging.h>
#include <rex/hook.h>
#include <rex/types.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>

namespace rex {
namespace kernel {
namespace xam {

namespace {

constexpr u32 kAvatarUnavailable = 0x8000FFFFu;

}  // namespace

u32 XamAvatarInitialize_entry(u32 unk1,                  // 1, 4, etc
                              u32 unk2,                  // 0 or 1
                              u32 processor_number,      // for thread creation?
                              mapped_u32 function_ptrs,  // 20b, 5 pointers
                              mapped_void unk5,          // ptr in data segment
                              u32 unk6                   // flags - 0x00300000, 0x30, etc
) {
  (void)unk1;
  (void)unk2;
  (void)processor_number;
  (void)function_ptrs;
  (void)unk5;
  (void)unk6;
  // Negative to fail. Game should immediately call XamAvatarShutdown.
  return ~0u;
}

void XamAvatarShutdown_entry() {
  // No-op.
}

u32 XamAvatarGetAssetsResultSize_entry(u32 asset_mask, mapped_u32 result_buffer_size_ptr,
                                       mapped_u32 gpu_resource_buffer_size_ptr) {
  (void)asset_mask;
  if (result_buffer_size_ptr) {
    *result_buffer_size_ptr = 0;
  }
  if (gpu_resource_buffer_size_ptr) {
    *gpu_resource_buffer_size_ptr = 0;
  }
  return kAvatarUnavailable;
}

u32 XamAvatarGetAssets_entry(mapped_void avatar_metadata_ptr, u32 asset_mask, u32 flags,
                             mapped_void result_buffer_ptr, mapped_void gpu_resource_buffer_ptr,
                             mapped_void overlapped_ptr) {
  (void)avatar_metadata_ptr;
  (void)asset_mask;
  (void)flags;
  (void)result_buffer_ptr;
  (void)gpu_resource_buffer_ptr;
  (void)overlapped_ptr;
  return kAvatarUnavailable;
}

u32 XamAvatarGetManifestLocalUser_entry(u32 user_index, mapped_void manifest_ptr,
                                        mapped_void overlapped_ptr) {
  (void)user_index;
  (void)manifest_ptr;
  (void)overlapped_ptr;
  return kAvatarUnavailable;
}

u32 XamAvatarGetMetadataRandom_entry(u32 body_type, u32 avatars_count,
                                     mapped_void metadata_ptr, mapped_void overlapped_ptr) {
  (void)body_type;
  (void)avatars_count;
  (void)metadata_ptr;
  (void)overlapped_ptr;
  return kAvatarUnavailable;
}

u32 XamAvatarGetMetadataSignedOutProfileCount_entry(mapped_u32 count_ptr,
                                                    mapped_void overlapped_ptr) {
  (void)overlapped_ptr;
  if (count_ptr) {
    *count_ptr = 0;
  }
  return kAvatarUnavailable;
}

u32 XamAvatarGetMetadataSignedOutProfile_entry(u32 profile_index, mapped_void metadata_ptr,
                                               mapped_void overlapped_ptr) {
  (void)profile_index;
  (void)metadata_ptr;
  (void)overlapped_ptr;
  return kAvatarUnavailable;
}

u32 XamAvatarLoadAnimation_entry(mapped_void asset_id_ptr, u32 flags, mapped_void output_ptr,
                                 mapped_void overlapped_ptr) {
  (void)asset_id_ptr;
  (void)flags;
  (void)output_ptr;
  (void)overlapped_ptr;
  return kAvatarUnavailable;
}

}  // namespace xam
}  // namespace kernel
}  // namespace rex

REX_EXPORT(__imp__XamAvatarInitialize, rex::kernel::xam::XamAvatarInitialize_entry)
REX_EXPORT(__imp__XamAvatarShutdown, rex::kernel::xam::XamAvatarShutdown_entry)

REX_EXPORT_STUB(__imp__XamAvatarBeginEnumAssets);
REX_EXPORT_STUB(__imp__XamAvatarEndEnumAssets);
REX_EXPORT_STUB(__imp__XamAvatarEnumAssets);
REX_EXPORT_STUB(__imp__XamAvatarGenerateMipMaps);
REX_EXPORT_STUB(__imp__XamAvatarGetAssetBinary);
REX_EXPORT_STUB(__imp__XamAvatarGetAssetIcon);
REX_EXPORT(__imp__XamAvatarGetAssets, rex::kernel::xam::XamAvatarGetAssets_entry)
REX_EXPORT(__imp__XamAvatarGetAssetsResultSize,
           rex::kernel::xam::XamAvatarGetAssetsResultSize_entry)
REX_EXPORT_STUB(__imp__XamAvatarGetInstalledAssetPackageDescription);
REX_EXPORT_STUB(__imp__XamAvatarGetInstrumentation);
REX_EXPORT(__imp__XamAvatarGetManifestLocalUser,
           rex::kernel::xam::XamAvatarGetManifestLocalUser_entry)
REX_EXPORT_STUB(__imp__XamAvatarGetManifestsByXuid);
REX_EXPORT(__imp__XamAvatarGetMetadataRandom,
           rex::kernel::xam::XamAvatarGetMetadataRandom_entry)
REX_EXPORT(__imp__XamAvatarGetMetadataSignedOutProfile,
           rex::kernel::xam::XamAvatarGetMetadataSignedOutProfile_entry)
REX_EXPORT(__imp__XamAvatarGetMetadataSignedOutProfileCount,
           rex::kernel::xam::XamAvatarGetMetadataSignedOutProfileCount_entry)
REX_EXPORT(__imp__XamAvatarLoadAnimation, rex::kernel::xam::XamAvatarLoadAnimation_entry)
REX_EXPORT_STUB(__imp__XamAvatarManifestGetBodyType);
REX_EXPORT_STUB(__imp__XamAvatarReinstallAwardedAsset);
REX_EXPORT_STUB(__imp__XamAvatarSetCustomAsset);
REX_EXPORT_STUB(__imp__XamAvatarSetManifest);
REX_EXPORT_STUB(__imp__XamAvatarSetMocks);
REX_EXPORT_STUB(__imp__XamAvatarWearNow);
