#include <catch2/catch_test_macros.hpp>

#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::kernel::xam {
u32 XamAvatarGetAssetsResultSize_entry(u32 asset_mask, mapped_u32 result_buffer_size_ptr,
                                       mapped_u32 gpu_resource_buffer_size_ptr);
u32 XamAvatarGetAssets_entry(mapped_void avatar_metadata_ptr, u32 asset_mask, u32 flags,
                             mapped_void result_buffer_ptr, mapped_void gpu_resource_buffer_ptr,
                             mapped_void overlapped_ptr);
u32 XamAvatarGetManifestLocalUser_entry(u32 user_index, mapped_void manifest_ptr,
                                        mapped_void overlapped_ptr);
u32 XamAvatarGetMetadataRandom_entry(u32 body_type, u32 avatars_count,
                                     mapped_void metadata_ptr, mapped_void overlapped_ptr);
u32 XamAvatarGetMetadataSignedOutProfileCount_entry(mapped_u32 count_ptr,
                                                    mapped_void overlapped_ptr);
u32 XamAvatarGetMetadataSignedOutProfile_entry(u32 profile_index, mapped_void metadata_ptr,
                                               mapped_void overlapped_ptr);
u32 XamAvatarLoadAnimation_entry(mapped_void asset_id_ptr, u32 flags, mapped_void output_ptr,
                                 mapped_void overlapped_ptr);
}  // namespace rex::kernel::xam

TEST_CASE("Avatar data calls fail deterministically while offline", "[kernel][xam_avatar]") {
  constexpr u32 kAvatarUnavailable = 0x8000FFFFu;

  rex::be_u32 result_buffer_size = 0xAAAAAAAAu;
  rex::be_u32 gpu_resource_buffer_size = 0xBBBBBBBBu;

  CHECK(rex::kernel::xam::XamAvatarGetAssetsResultSize_entry(
            1, mapped_u32(&result_buffer_size, 0x40001000),
            mapped_u32(&gpu_resource_buffer_size, 0x40001004)) == kAvatarUnavailable);
  CHECK(static_cast<u32>(result_buffer_size) == 0);
  CHECK(static_cast<u32>(gpu_resource_buffer_size) == 0);

  CHECK(rex::kernel::xam::XamAvatarGetAssets_entry(
            mapped_void(nullptr), 1, 0, mapped_void(nullptr), mapped_void(nullptr),
            mapped_void(nullptr)) == kAvatarUnavailable);
  CHECK(rex::kernel::xam::XamAvatarGetManifestLocalUser_entry(
            0, mapped_void(nullptr), mapped_void(nullptr)) == kAvatarUnavailable);
  CHECK(rex::kernel::xam::XamAvatarGetMetadataRandom_entry(
            1, 1, mapped_void(nullptr), mapped_void(nullptr)) == kAvatarUnavailable);
  CHECK(rex::kernel::xam::XamAvatarGetMetadataSignedOutProfile_entry(
            0, mapped_void(nullptr), mapped_void(nullptr)) == kAvatarUnavailable);
  CHECK(rex::kernel::xam::XamAvatarLoadAnimation_entry(
            mapped_void(nullptr), 0, mapped_void(nullptr), mapped_void(nullptr)) ==
        kAvatarUnavailable);
}

TEST_CASE("Avatar signed-out profile count reports zero while offline",
          "[kernel][xam_avatar]") {
  constexpr u32 kAvatarUnavailable = 0x8000FFFFu;

  rex::be_u32 profile_count = 0xBBBBBBBBu;

  CHECK(rex::kernel::xam::XamAvatarGetMetadataSignedOutProfileCount_entry(
            mapped_u32(&profile_count, 0x40001000), mapped_void(nullptr)) ==
        kAvatarUnavailable);
  CHECK(static_cast<u32>(profile_count) == 0);

  CHECK(rex::kernel::xam::XamAvatarGetMetadataSignedOutProfileCount_entry(
            mapped_u32(nullptr), mapped_void(nullptr)) == kAvatarUnavailable);
}
