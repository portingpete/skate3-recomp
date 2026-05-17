#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include <rex/ppc/context.h>
#include <rex/types.h>

extern "C" REX_FUNC(__imp__RtlCaptureContext);
extern "C" REX_FUNC(__imp__RtlUnwind);
extern "C" REX_FUNC(__imp____C_specific_handler);

namespace rex::kernel::xboxkrnl {
u32 RtlUpcaseUnicodeChar_entry(u32 in);
u32 RtlDowncaseUnicodeChar_entry(u32 in);
}  // namespace rex::kernel::xboxkrnl

namespace {

uint32_t ReadBe32(const uint8_t* data, size_t offset) {
  return (uint32_t(data[offset]) << 24) | (uint32_t(data[offset + 1]) << 16) |
         (uint32_t(data[offset + 2]) << 8) | uint32_t(data[offset + 3]);
}

uint64_t ReadBe64(const uint8_t* data, size_t offset) {
  return (uint64_t(ReadBe32(data, offset)) << 32) | ReadBe32(data, offset + 4);
}

}  // namespace

TEST_CASE("Unicode case helpers map ASCII letters and preserve other codepoints",
          "[kernel][xboxkrnl][rtl]") {
  CHECK(rex::kernel::xboxkrnl::RtlUpcaseUnicodeChar_entry('a') == 'A');
  CHECK(rex::kernel::xboxkrnl::RtlUpcaseUnicodeChar_entry('Z') == 'Z');
  CHECK(rex::kernel::xboxkrnl::RtlUpcaseUnicodeChar_entry(0x00E9) == 0x00E9);

  CHECK(rex::kernel::xboxkrnl::RtlDowncaseUnicodeChar_entry('A') == 'a');
  CHECK(rex::kernel::xboxkrnl::RtlDowncaseUnicodeChar_entry('z') == 'z');
  CHECK(rex::kernel::xboxkrnl::RtlDowncaseUnicodeChar_entry(0x00C9) == 0x00C9);
}

TEST_CASE("RtlCaptureContext writes guest nonvolatile context records",
          "[kernel][xboxkrnl][rtl]") {
  constexpr uint32_t kContextAddress = 0x1000;
  std::array<uint8_t, 0x2000> storage{};

  PPCContext ctx{};
  ctx.r3.u32 = kContextAddress;
  ctx.r1.u64 = 0x000000007C24F480ull;
  ctx.r13.u64 = 0x0000000030009000ull;
  ctx.r14.u64 = 0x1111222233334444ull;
  ctx.r31.u64 = 0xAAAABBBBCCCCDDDDull;
  ctx.f14.u64 = 0x0102030405060708ull;
  ctx.f31.u64 = 0xF1F2F3F4F5F6F7F8ull;
  ctx.lr = 0x82792EECu;
  ctx.cr0.set_raw(0x8);
  ctx.cr1.set_raw(0x4);
  ctx.cr2.set_raw(0x2);
  ctx.cr3.set_raw(0x1);
  ctx.cr4.set_raw(0x8);
  ctx.cr5.set_raw(0x4);
  ctx.cr6.set_raw(0x2);
  ctx.cr7.set_raw(0x1);
  for (uint32_t i = 0; i < 16; ++i) {
    ctx.v64.u8[i] = uint8_t(i);
    ctx.v127.u8[i] = uint8_t(0xF0u + i);
  }

  __imp__RtlCaptureContext(ctx, storage.data());

  const auto* record = storage.data() + kContextAddress;
  CHECK(ReadBe64(record, 0) == 0x0102030405060708ull);
  CHECK(ReadBe64(record, 17 * 8) == 0xF1F2F3F4F5F6F7F8ull);
  CHECK(ReadBe64(record, 144) == 0x000000007C24F480ull);
  CHECK(ReadBe64(record, 152) == 0x0000000030009000ull);
  CHECK(ReadBe64(record, 160) == 0x1111222233334444ull);
  CHECK(ReadBe64(record, 152 + 18 * 8) == 0xAAAABBBBCCCCDDDDull);
  CHECK(ReadBe32(record, 304) == 0x84218421u);
  CHECK(ReadBe32(record, 308) == 0x82792EECu);
  CHECK(ReadBe32(record, 8) == 0x82792EECu);
  CHECK(ReadBe64(record, 32) == 0x000000007C24F480ull);

  CHECK(record[320] == 15);
  CHECK(record[320 + 15] == 0);
  CHECK(record[320 + 63 * 16] == 0xFF);
  CHECK(record[320 + 63 * 16 + 15] == 0xF0);
}

TEST_CASE("RtlUnwind leaves a deterministic guest return value",
          "[kernel][xboxkrnl][rtl]") {
  std::array<uint8_t, 0x1000> storage{};
  PPCContext ctx{};
  ctx.r3.u32 = 0x7C24F480u;  // TargetFrame
  ctx.r4.u32 = 0x825EEC04u;  // TargetIp
  ctx.r5.u32 = 0x00000800u;  // ExceptionRecord
  ctx.r6.u32 = 0xC0FFEE42u;  // ReturnValue

  __imp__RtlUnwind(ctx, storage.data());

  CHECK(ctx.r3.u32 == 0xC0FFEE42u);
}

TEST_CASE("__C_specific_handler continues handler search when dispatcher state is unavailable",
          "[kernel][xboxkrnl][rtl]") {
  std::array<uint8_t, 0x1000> storage{};
  PPCContext ctx{};
  ctx.r3.u32 = 0x100u;       // ExceptionRecord
  ctx.r4.u32 = 0x7C24F480u;  // EstablisherFrame
  ctx.r5.u32 = 0x200u;       // ContextRecord
  ctx.r6.u32 = 0x300u;       // DispatcherContext

  __imp____C_specific_handler(ctx, storage.data());

  CHECK(ctx.r3.u32 == 1u);
}
