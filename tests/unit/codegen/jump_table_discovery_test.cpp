#include <cstdint>
#include <span>
#include <vector>

#define _ALLOW_KEYWORD_MACROS
#define private public
#include <rex/codegen/binary_view.h>
#undef private

#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/codegen_context.h>
#include <rex/codegen/phases.h>

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

void AppendBe32(std::vector<uint8_t>& bytes, uint32_t value) {
  bytes.push_back(static_cast<uint8_t>(value >> 24));
  bytes.push_back(static_cast<uint8_t>(value >> 16));
  bytes.push_back(static_cast<uint8_t>(value >> 8));
  bytes.push_back(static_cast<uint8_t>(value));
}

}  // namespace

TEST_CASE("Discover honors jump table bounds above the old 512 entry cap",
          "[codegen][Discover][JumpTable]") {
  constexpr uint32_t kBase = 0x1000;
  constexpr uint32_t kEntryCount = 643;
  constexpr uint32_t kTableAddress = 0x1020;
  constexpr uint32_t kCaseAddress = kTableAddress + kEntryCount * 4;

  std::vector<uint8_t> bytes;
  bytes.reserve((kCaseAddress - kBase) + 8);
  AppendBe32(bytes, 0x39600000);  // 0x1000: li r11,0
  AppendBe32(bytes, 0x280B0282);  // 0x1004: cmplwi r11,642
  AppendBe32(bytes, 0x3D800000);  // 0x1008: lis r12,0
  AppendBe32(bytes, 0x398C1020);  // 0x100C: addi r12,r12,0x1020
  AppendBe32(bytes, 0x5560103A);  // 0x1010: rlwinm r0,r11,2,0,29
  AppendBe32(bytes, 0x7C0C002E);  // 0x1014: lwzx r0,r12,r0
  AppendBe32(bytes, 0x7C0903A6);  // 0x1018: mtctr r0
  AppendBe32(bytes, 0x4E800420);  // 0x101C: bctr

  for (uint32_t i = 0; i < kEntryCount; ++i) {
    AppendBe32(bytes, kCaseAddress);
  }
  AppendBe32(bytes, 0x38600001);  // li r3,1
  AppendBe32(bytes, 0x4E800020);  // blr

  auto binary = MakeBinaryView(kBase, bytes);
  rex::codegen::RecompilerConfig config;
  auto ctx = rex::codegen::CodegenContext::Create(std::move(binary), std::move(config));
  ctx.initDecoded();

  ctx.scan.codeRegions.push_back({kBase, kBase + static_cast<uint32_t>(bytes.size())});
  ctx.graph.addFunction(kBase, 0x20, rex::codegen::FunctionAuthority::PDATA, true);

  auto discoverResult = rex::codegen::phases::Discover(ctx);
  REQUIRE(discoverResult.has_value());

  const auto* node = ctx.graph.getFunction(kBase);
  REQUIRE(node != nullptr);
  REQUIRE(node->jumpTables().size() == 1);
  CHECK(node->jumpTables()[0].targets.size() == kEntryCount);
  CHECK(node->jumpTables()[0].targets.back() == kCaseAddress);
}

TEST_CASE("Discover reads large unbounded jump tables past the old 512 entry cap",
          "[codegen][Discover][JumpTable]") {
  constexpr uint32_t kBase = 0x1000;
  constexpr uint32_t kEntryCount = 643;
  constexpr uint32_t kTableAddress = 0x101C;
  constexpr uint32_t kCaseAddress = kTableAddress + kEntryCount * 4 + 4;

  std::vector<uint8_t> bytes;
  bytes.reserve((kCaseAddress - kBase) + 8);
  AppendBe32(bytes, 0x39600000);  // 0x1000: li r11,0
  AppendBe32(bytes, 0x3D800000);  // 0x1004: lis r12,0
  AppendBe32(bytes, 0x398C101C);  // 0x1008: addi r12,r12,0x101C
  AppendBe32(bytes, 0x5560103A);  // 0x100C: rlwinm r0,r11,2,0,29
  AppendBe32(bytes, 0x7C0C002E);  // 0x1010: lwzx r0,r12,r0
  AppendBe32(bytes, 0x7C0903A6);  // 0x1014: mtctr r0
  AppendBe32(bytes, 0x4E800420);  // 0x1018: bctr

  for (uint32_t i = 0; i < kEntryCount; ++i) {
    AppendBe32(bytes, kCaseAddress);
  }
  AppendBe32(bytes, 0x00000000);  // table terminator/padding
  AppendBe32(bytes, 0x38600001);  // li r3,1
  AppendBe32(bytes, 0x4E800020);  // blr

  auto binary = MakeBinaryView(kBase, bytes);
  rex::codegen::RecompilerConfig config;
  auto ctx = rex::codegen::CodegenContext::Create(std::move(binary), std::move(config));
  ctx.initDecoded();

  ctx.scan.codeRegions.push_back({kBase, kBase + static_cast<uint32_t>(bytes.size())});
  ctx.graph.addFunction(kBase, 0x1C, rex::codegen::FunctionAuthority::PDATA, true);

  auto discoverResult = rex::codegen::phases::Discover(ctx);
  REQUIRE(discoverResult.has_value());

  const auto* node = ctx.graph.getFunction(kBase);
  REQUIRE(node != nullptr);
  REQUIRE(node->jumpTables().size() == 1);
  CHECK(node->jumpTables()[0].targets.size() == kEntryCount);
  CHECK(node->jumpTables()[0].targets.back() == kCaseAddress);
}
