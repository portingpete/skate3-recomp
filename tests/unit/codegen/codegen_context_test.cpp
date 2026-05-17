#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

#define _ALLOW_KEYWORD_MACROS
#define private public
#include <rex/codegen/binary_view.h>
#undef private

#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/codegen_context.h>
#include <rex/codegen/config.h>

namespace {

rex::codegen::BinaryView MakeBinaryView(uint32_t base, std::span<const uint8_t> bytes) {
  rex::codegen::BinaryView view;
  view.sectionNames_.push_back(".text");
  view.sectionData_.emplace_back(bytes.begin(), bytes.end());
  view.sections_.push_back({
      .name = view.sectionNames_.back(),
      .baseAddress = base,
      .size = static_cast<uint32_t>(bytes.size()),
      .data = view.sectionData_.back().data(),
      .executable = true,
  });
  return view;
}

}  // namespace

TEST_CASE("CodegenContext detects BFME2-shaped setjmp and longjmp helpers",
          "[codegen][context]") {
  constexpr uint32_t kLongJmp = 0x82A91460;
  constexpr uint32_t kSetJmp = 0x82A916B0;
  constexpr size_t kSetJmpOffset = kSetJmp - kLongJmp;

  std::array<uint8_t, kSetJmpOffset + 32> bytes{};
  constexpr std::array<uint8_t, 28> kBfme2LongJmp = {
      0x7C, 0x08, 0x02, 0xA6,  // mflr r0
      0x94, 0x21, 0xFF, 0xB0,  // stwu r1,-80(r1)
      0x90, 0x01, 0x00, 0x08,  // stw r0,8(r1)
      0x7C, 0x86, 0x23, 0x78,  // mr r6,r4
      0x2C, 0x04, 0x00, 0x00,  // cmpwi r4,0
      0x80, 0x03, 0x01, 0x38,  // lwz r0,312(r3)
      0x2C, 0x80, 0x00, 0x00,  // cmpwi cr1,r0,0
  };
  constexpr std::array<uint8_t, 28> kBfme2SetJmp = {
      0x3C, 0x80, 0x83, 0x16,  // lis r4,0x8316
      0x80, 0x04, 0x09, 0x64,  // lwz r0,2404(r4)
      0x2C, 0x00, 0x00, 0x00,  // cmpwi r0,0
      0x7C, 0x09, 0x03, 0xA6,  // mtctr r0
      0x4C, 0x82, 0x04, 0x20,  // bnectr cr0
      0x7C, 0x08, 0x02, 0xA6,  // mflr r0
      0x7C, 0x80, 0x00, 0x26,  // mfcr r4
  };

  std::copy(kBfme2LongJmp.begin(), kBfme2LongJmp.end(), bytes.begin());
  std::copy(kBfme2SetJmp.begin(), kBfme2SetJmp.end(), bytes.begin() + kSetJmpOffset);

  rex::codegen::RecompilerConfig config;
  auto ctx = rex::codegen::CodegenContext::Create(MakeBinaryView(kLongJmp, bytes), config);

  CHECK(ctx.Config().longJmpAddress == kLongJmp);
  CHECK(ctx.Config().setJmpAddress == kSetJmp);
}
