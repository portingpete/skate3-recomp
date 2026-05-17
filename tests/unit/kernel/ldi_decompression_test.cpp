#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::kernel::xboxkrnl {
u32 LDICreateDecompression_entry(mapped_u32 window_size_ptr, mapped_u32 chunk_size_ptr,
                                 mapped_void alloc_proc, mapped_void free_proc, u32 memory_size,
                                 mapped_u32 workspace_size_ptr, mapped_u32 context_out_ptr);
u32 LDIDecompress_entry(u32 handle, mapped_void input, u32 input_size, mapped_void output,
                        mapped_u32 output_size_ptr);
u32 LDIResetDecompression_entry(u32 handle);
u32 LDIDestroyDecompression_entry(u32 handle);
}  // namespace rex::kernel::xboxkrnl

namespace {

constexpr u32 kStatusSuccess = 0x00000000u;
constexpr u32 kStatusInvalidHandle = 0xC0000008u;

uint32_t ReadBe32(const uint8_t* data, size_t offset) {
  return (uint32_t(data[offset]) << 24) | (uint32_t(data[offset + 1]) << 16) |
         (uint32_t(data[offset + 2]) << 8) | uint32_t(data[offset + 3]);
}

void WriteBe32(uint8_t* data, size_t offset, uint32_t value) {
  data[offset] = uint8_t(value >> 24);
  data[offset + 1] = uint8_t(value >> 16);
  data[offset + 2] = uint8_t(value >> 8);
  data[offset + 3] = uint8_t(value);
}

uint8_t* AlignedBase(std::vector<uint8_t>& storage) {
  const uintptr_t raw = reinterpret_cast<uintptr_t>(storage.data());
  return reinterpret_cast<uint8_t*>((raw + 31u) & ~uintptr_t{31u});
}

uint32_t CreateLdiHandle(uint8_t* base) {
  constexpr uint32_t kWindowSizePtr = 0x100;
  constexpr uint32_t kChunkSizePtr = 0x104;
  constexpr uint32_t kWorkspaceSizePtr = 0x108;
  constexpr uint32_t kHandlePtr = 0x10C;

  WriteBe32(base, kWindowSizePtr, 0x8000);
  WriteBe32(base, kChunkSizePtr, 0x8000);
  WriteBe32(base, kWorkspaceSizePtr, 0x9800);
  WriteBe32(base, kHandlePtr, 0);

  const uint32_t result = rex::kernel::xboxkrnl::LDICreateDecompression_entry(
      mapped_u32(reinterpret_cast<rex::be_u32*>(base + kWindowSizePtr), kWindowSizePtr),
      mapped_u32(reinterpret_cast<rex::be_u32*>(base + kChunkSizePtr), kChunkSizePtr),
      mapped_void(nullptr), mapped_void(nullptr), 0x19800,
      mapped_u32(reinterpret_cast<rex::be_u32*>(base + kWorkspaceSizePtr), kWorkspaceSizePtr),
      mapped_u32(reinterpret_cast<rex::be_u32*>(base + kHandlePtr), kHandlePtr));

  CHECK(result == kStatusSuccess);
  CHECK(ReadBe32(base, kWindowSizePtr) >= 0x8000);
  CHECK(ReadBe32(base, kWorkspaceSizePtr) >= 0x9800);
  const uint32_t handle = ReadBe32(base, kHandlePtr);
  CHECK(handle != 0);
  return handle;
}

}  // namespace

TEST_CASE("LDI creates resettable opaque decompression handles", "[kernel][xboxkrnl][ldi]") {
  std::vector<uint8_t> storage(0x2000 + 0x20);
  uint8_t* base = AlignedBase(storage);

  const uint32_t handle = CreateLdiHandle(base);

  CHECK(rex::kernel::xboxkrnl::LDIResetDecompression_entry(handle) == kStatusSuccess);
  CHECK(rex::kernel::xboxkrnl::LDIDestroyDecompression_entry(handle) == kStatusSuccess);
  CHECK(rex::kernel::xboxkrnl::LDIDestroyDecompression_entry(handle) == kStatusInvalidHandle);
}

TEST_CASE("LDI decompresses a single raw LZX chunk", "[kernel][xboxkrnl][ldi]") {
  std::vector<uint8_t> storage(0x2000 + 0x20);
  uint8_t* base = AlignedBase(storage);
  const uint32_t handle = CreateLdiHandle(base);

  constexpr uint32_t kInput = 0x200;
  constexpr uint32_t kOutput = 0x400;
  constexpr uint32_t kOutputSizePtr = 0x700;
  constexpr std::array<uint8_t, 6> kExpected = {'R', 'e', 'x', 'L', 'D', 'I'};
  constexpr std::array<uint8_t, 22> kStoredLzx = {
      0x00, 0x30, 0x60, 0x00,  // no Intel header, uncompressed block, length 6
      0x00, 0x00, 0x00, 0x00,  // R0
      0x00, 0x00, 0x00, 0x00,  // R1
      0x00, 0x00, 0x00, 0x00,  // R2
      'R',  'e',  'x',  'L',  'D',  'I',
  };

  std::memcpy(base + kInput, kStoredLzx.data(), kStoredLzx.size());
  WriteBe32(base, kOutputSizePtr, uint32_t(kExpected.size()));

  const uint32_t result = rex::kernel::xboxkrnl::LDIDecompress_entry(
      handle, mapped_void(base + kInput, kInput), uint32_t(kStoredLzx.size()),
      mapped_void(base + kOutput, kOutput),
      mapped_u32(reinterpret_cast<rex::be_u32*>(base + kOutputSizePtr), kOutputSizePtr));

  CHECK(result == kStatusSuccess);
  CHECK(ReadBe32(base, kOutputSizePtr) == kExpected.size());
  CHECK(std::memcmp(base + kOutput, kExpected.data(), kExpected.size()) == 0);

  CHECK(rex::kernel::xboxkrnl::LDIDestroyDecompression_entry(handle) == kStatusSuccess);
}
