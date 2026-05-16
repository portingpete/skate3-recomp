#include <array>
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

rex::codegen::BinaryView MakeBinaryViewWithRdata(uint32_t textBase, std::span<const uint8_t> text,
                                                 uint32_t rdataBase,
                                                 std::span<const uint8_t> rdata) {
  rex::codegen::BinaryView view;
  view.sectionNames_.reserve(2);
  view.sectionData_.reserve(2);
  view.sections_.reserve(2);

  view.sectionNames_.push_back(".text");
  view.sectionData_.emplace_back(text.begin(), text.end());
  view.sections_.push_back({
      .name = view.sectionNames_.back(),
      .baseAddress = textBase,
      .size = static_cast<uint32_t>(text.size()),
      .data = view.sectionData_.back().data(),
      .executable = true,
  });

  view.sectionNames_.push_back(".rdata");
  view.sectionData_.emplace_back(rdata.begin(), rdata.end());
  view.sections_.push_back({
      .name = view.sectionNames_.back(),
      .baseAddress = rdataBase,
      .size = static_cast<uint32_t>(rdata.size()),
      .data = view.sectionData_.back().data(),
      .executable = false,
  });

  return view;
}

}  // namespace

TEST_CASE("Discover registers code pointers returned by helper thunks",
          "[codegen][Discover][FunctionPointer]") {
  constexpr std::array<uint8_t, 28> kReturnedCodePointerThunk = {
      0x38, 0x60, 0x00, 0x2A,  // 0x1000: li r3,42
      0x4E, 0x80, 0x00, 0x20,  // 0x1004: blr
      0x00, 0x00, 0x00, 0x00,  // 0x1008: padding
      0x00, 0x00, 0x00, 0x00,  // 0x100C: padding
      0x3D, 0x60, 0x00, 0x00,  // 0x1010: lis r11,0
      0x38, 0x6B, 0x10, 0x00,  // 0x1014: addi r3,r11,0x1000
      0x4E, 0x80, 0x00, 0x20,  // 0x1018: blr
  };

  auto binary = MakeBinaryView(0x1000, kReturnedCodePointerThunk);
  rex::codegen::RecompilerConfig config;
  auto ctx = rex::codegen::CodegenContext::Create(std::move(binary), std::move(config));
  ctx.initDecoded();

  ctx.scan.codeRegions.push_back({0x1000, 0x101C});
  ctx.graph.addFunction(0x1010, 12, rex::codegen::FunctionAuthority::PDATA, true);

  auto result = rex::codegen::phases::Discover(ctx);
  REQUIRE(result.has_value());
  CHECK(ctx.graph.isEntryPoint(0x1000));
}

TEST_CASE("Discover registers raw rdata code-pointer table thunks",
          "[codegen][Discover][FunctionPointer]") {
  constexpr std::array<uint8_t, 24> kTinyAdjacentThunks = {
      0x38, 0x60, 0x00, 0x01,  // 0x1000: li r3,1
      0x4E, 0x80, 0x00, 0x20,  // 0x1004: blr
      0x38, 0x63, 0xFF, 0xFC,  // 0x1008: addi r3,r3,-4
      0x4E, 0x80, 0x00, 0x20,  // 0x100C: blr
      0x38, 0x60, 0x00, 0x02,  // 0x1010: li r3,2
      0x4E, 0x80, 0x00, 0x20,  // 0x1014: blr
  };
  constexpr std::array<uint8_t, 16> kPointerTable = {
      0x00, 0x00, 0x10, 0x00,  // .rdata[0] -> known function
      0x00, 0x00, 0x10, 0x08,  // .rdata[1] -> missing tiny thunk
      0x00, 0x00, 0x10, 0x10,  // .rdata[2] -> known function
      0x00, 0x00, 0x00, 0x00,  // terminator
  };

  auto binary = MakeBinaryViewWithRdata(0x1000, kTinyAdjacentThunks, 0x2000, kPointerTable);
  rex::codegen::RecompilerConfig config;
  auto ctx = rex::codegen::CodegenContext::Create(std::move(binary), std::move(config));
  ctx.initDecoded();

  ctx.scan.codeRegions.push_back({0x1000, 0x1018});
  ctx.graph.addFunction(0x1000, 8, rex::codegen::FunctionAuthority::PDATA, true);
  ctx.graph.addFunction(0x1010, 8, rex::codegen::FunctionAuthority::PDATA, true);

  auto result = rex::codegen::phases::Discover(ctx);
  REQUIRE(result.has_value());
  CHECK(ctx.graph.isEntryPoint(0x1008));
}

TEST_CASE("Discover registers raw rdata pointers to callable internal labels",
          "[codegen][Discover][FunctionPointer]") {
  constexpr std::array<uint8_t, 32> kCallbackEntriesWithInternalLabel = {
      0x38, 0x60, 0x00, 0x01,  // 0x1000: li r3,1
      0x4E, 0x80, 0x00, 0x20,  // 0x1004: blr
      0x48, 0x00, 0x00, 0x08,  // 0x1008: b 0x1010
      0x60, 0x00, 0x00, 0x00,  // 0x100C: nop
      0x38, 0x60, 0x00, 0x02,  // 0x1010: li r3,2
      0x4E, 0x80, 0x00, 0x20,  // 0x1014: blr
      0x38, 0x60, 0x00, 0x03,  // 0x1018: li r3,3
      0x4E, 0x80, 0x00, 0x20,  // 0x101C: blr
  };
  constexpr std::array<uint8_t, 20> kCallbackTable = {
      0x00, 0xEB, 0x00, 0x0D,  // resource type id, not a code pointer
      0x00, 0x00, 0x10, 0x00,  // table slot -> known function
      0x00, 0x00, 0x10, 0x08,  // table slot -> known function
      0x00, 0x00, 0x10, 0x10,  // table slot -> internal alternate entry
      0x00, 0x00, 0x00, 0x00,  // terminator
  };

  auto binary =
      MakeBinaryViewWithRdata(0x1000, kCallbackEntriesWithInternalLabel, 0x2000, kCallbackTable);
  rex::codegen::RecompilerConfig config;
  auto ctx = rex::codegen::CodegenContext::Create(std::move(binary), std::move(config));
  ctx.initDecoded();

  ctx.scan.codeRegions.push_back({0x1000, 0x1020});
  ctx.scan.pdataSizes[0x1000] = 0x08;
  ctx.scan.pdataSizes[0x1008] = 0x10;
  ctx.graph.addFunction(0x1000, 0x08, rex::codegen::FunctionAuthority::PDATA, true);
  ctx.graph.addFunction(0x1008, 0x10, rex::codegen::FunctionAuthority::PDATA, true);

  auto result = rex::codegen::phases::Discover(ctx);
  REQUIRE(result.has_value());
  CHECK(ctx.graph.isEntryPoint(0x1010));
}

TEST_CASE("Discover does not promote interior-only raw rdata label tables",
          "[codegen][Discover][FunctionPointer]") {
  constexpr std::array<uint8_t, 16> kLinearFunction = {
      0x60, 0x00, 0x00, 0x00,  // 0x1000: nop
      0x60, 0x00, 0x00, 0x00,  // 0x1004: nop
      0x60, 0x00, 0x00, 0x00,  // 0x1008: nop
      0x4E, 0x80, 0x00, 0x20,  // 0x100C: blr
  };
  constexpr std::array<uint8_t, 16> kInteriorLabelTable = {
      0x00, 0x00, 0x10, 0x04,  // table slot -> interior instruction
      0x00, 0x00, 0x10, 0x08,  // table slot -> interior instruction
      0x00, 0x00, 0x10, 0x0C,  // table slot -> interior instruction
      0x00, 0x00, 0x00, 0x00,  // terminator
  };

  auto binary = MakeBinaryViewWithRdata(0x1000, kLinearFunction, 0x2000, kInteriorLabelTable);
  rex::codegen::RecompilerConfig config;
  auto ctx = rex::codegen::CodegenContext::Create(std::move(binary), std::move(config));
  ctx.initDecoded();

  ctx.scan.codeRegions.push_back({0x1000, 0x1010});
  ctx.scan.pdataSizes[0x1000] = 0x10;
  ctx.graph.addFunction(0x1000, 0x10, rex::codegen::FunctionAuthority::PDATA, true);

  auto result = rex::codegen::phases::Discover(ctx);
  REQUIRE(result.has_value());
  CHECK_FALSE(ctx.graph.isEntryPoint(0x1004));
  CHECK_FALSE(ctx.graph.isEntryPoint(0x1008));
  CHECK_FALSE(ctx.graph.isEntryPoint(0x100C));
}

TEST_CASE("GapFill returned-pointer thunks register alternate entries inside known functions",
          "[codegen][GapFill][FunctionPointer]") {
  constexpr std::array<uint8_t, 44> kGapFilledReturnedCodePointerThunk = {
      0x38, 0x80, 0x00, 0x01,  // 0x1000: li r4,1
      0x38, 0xA0, 0x00, 0x02,  // 0x1004: li r5,2
      0x38, 0x60, 0x00, 0x2A,  // 0x1008: li r3,42
      0x4E, 0x80, 0x00, 0x20,  // 0x100C: blr
      0x00, 0x00, 0x00, 0x00,  // 0x1010: padding
      0x00, 0x00, 0x00, 0x00,  // 0x1014: padding
      0x00, 0x00, 0x00, 0x00,  // 0x1018: padding
      0x00, 0x00, 0x00, 0x00,  // 0x101C: padding
      0x3D, 0x60, 0x00, 0x00,  // 0x1020: lis r11,0
      0x38, 0x6B, 0x10, 0x08,  // 0x1024: addi r3,r11,0x1008
      0x4E, 0x80, 0x00, 0x20,  // 0x1028: blr
  };

  auto binary = MakeBinaryView(0x1000, kGapFilledReturnedCodePointerThunk);
  rex::codegen::RecompilerConfig config;
  auto ctx = rex::codegen::CodegenContext::Create(std::move(binary), std::move(config));
  ctx.initDecoded();

  ctx.scan.codeRegions.push_back({0x1000, 0x1010});
  ctx.scan.codeRegions.push_back({0x1020, 0x102C});
  ctx.scan.pdataSizes[0x1000] = 16;
  ctx.graph.addFunction(0x1000, 16, rex::codegen::FunctionAuthority::PDATA, true);

  auto discoverResult = rex::codegen::phases::Discover(ctx);
  REQUIRE(discoverResult.has_value());
  CHECK_FALSE(ctx.graph.isEntryPoint(0x1008));

  auto gapFillResult = rex::codegen::phases::GapFill(ctx);
  REQUIRE(gapFillResult.has_value());
  CHECK(ctx.graph.isEntryPoint(0x1020));
  CHECK(ctx.graph.isEntryPoint(0x1008));
}

TEST_CASE("GapFill splits uncovered code after unconditional bctr", "[codegen][GapFill]") {
  constexpr std::array<uint8_t, 12> kBctrThenLeaf = {
      0x4E, 0x80, 0x04, 0x20,  // 0x1000: bctr
      0x38, 0x60, 0x00, 0x10,  // 0x1004: li r3,16
      0x4E, 0x80, 0x00, 0x20,  // 0x1008: blr
  };

  auto binary = MakeBinaryView(0x1000, kBctrThenLeaf);
  rex::codegen::RecompilerConfig config;
  auto ctx = rex::codegen::CodegenContext::Create(std::move(binary), std::move(config));
  ctx.initDecoded();

  ctx.scan.codeRegions.push_back({0x1000, 0x100C});
  ctx.graph.addFunction(0x1000, 4, rex::codegen::FunctionAuthority::VTABLE, true);

  auto result = rex::codegen::phases::GapFill(ctx);
  REQUIRE(result.has_value());
  CHECK(ctx.graph.isEntryPoint(0x1004));
}

TEST_CASE("GapFill does not promote discovered absolute jump table data",
          "[codegen][GapFill][JumpTable]") {
  constexpr std::array<uint8_t, 64> kSwitchWithInlineTable = {
      0x39, 0x40, 0x00, 0x00,  // 0x1000: li r10,0
      0x3D, 0x80, 0x00, 0x00,  // 0x1004: lis r12,0
      0x39, 0x8C, 0x10, 0x1C,  // 0x1008: addi r12,r12,0x101C
      0x55, 0x40, 0x10, 0x3A,  // 0x100C: rlwinm r0,r10,2,0,29
      0x7C, 0x0C, 0x00, 0x2E,  // 0x1010: lwzx r0,r12,r0
      0x7C, 0x09, 0x03, 0xA6,  // 0x1014: mtctr r0
      0x4E, 0x80, 0x04, 0x20,  // 0x1018: bctr
      0x00, 0x00, 0x10, 0x2C,  // 0x101C: table[0] -> 0x102C
      0x00, 0x00, 0x10, 0x34,  // 0x1020: table[1] -> 0x1034
      0x00, 0x00, 0x00, 0x00,  // 0x1024: table terminator/padding
      0x00, 0x00, 0x00, 0x00,  // 0x1028: padding
      0x38, 0x60, 0x00, 0x01,  // 0x102C: li r3,1
      0x4E, 0x80, 0x00, 0x20,  // 0x1030: blr
      0x38, 0x60, 0x00, 0x02,  // 0x1034: li r3,2
      0x4E, 0x80, 0x00, 0x20,  // 0x1038: blr
      0x00, 0x00, 0x00, 0x00,  // 0x103C: padding
  };

  auto binary = MakeBinaryView(0x1000, kSwitchWithInlineTable);
  rex::codegen::RecompilerConfig config;
  auto ctx = rex::codegen::CodegenContext::Create(std::move(binary), std::move(config));
  ctx.initDecoded();

  ctx.scan.codeRegions.push_back({0x1000, 0x1040});
  ctx.graph.addFunction(0x1000, 4, rex::codegen::FunctionAuthority::DISCOVERED, true);

  auto discoverResult = rex::codegen::phases::Discover(ctx);
  REQUIRE(discoverResult.has_value());

  const auto* node = ctx.graph.getFunction(0x1000);
  REQUIRE(node != nullptr);
  REQUIRE(node->jumpTables().size() == 1);
  CHECK(node->jumpTables()[0].tableAddress == 0x101C);

  auto gapFillResult = rex::codegen::phases::GapFill(ctx);
  REQUIRE(gapFillResult.has_value());
  CHECK_FALSE(ctx.graph.isEntryPoint(0x101C));
}
