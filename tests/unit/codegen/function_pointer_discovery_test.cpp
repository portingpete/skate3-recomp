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

TEST_CASE("Discover registers dynamically stored code-pointer entries",
          "[codegen][Discover][FunctionPointer]") {
  auto kDynamicCodePointerStore = [] {
    std::array<uint8_t, 0x50> bytes{};

    bytes[0x00] = 0x38;  // 0x1000: li r3,1
    bytes[0x01] = 0x60;
    bytes[0x02] = 0x00;
    bytes[0x03] = 0x01;
    bytes[0x04] = 0x4E;  // 0x1004: blr
    bytes[0x05] = 0x80;
    bytes[0x06] = 0x00;
    bytes[0x07] = 0x20;
    bytes[0x18] = 0x38;  // 0x1018: li r3,2
    bytes[0x19] = 0x60;
    bytes[0x1A] = 0x00;
    bytes[0x1B] = 0x02;
    bytes[0x1C] = 0x4E;  // 0x101C: blr
    bytes[0x1D] = 0x80;
    bytes[0x1E] = 0x00;
    bytes[0x1F] = 0x20;

    bytes[0x40] = 0x3D;  // 0x1040: lis r11,0
    bytes[0x41] = 0x60;
    bytes[0x42] = 0x00;
    bytes[0x43] = 0x00;
    bytes[0x44] = 0x39;  // 0x1044: addi r8,r11,0x1018
    bytes[0x45] = 0x0B;
    bytes[0x46] = 0x10;
    bytes[0x47] = 0x18;
    bytes[0x48] = 0x91;  // 0x1048: stw r8,16(r3)
    bytes[0x49] = 0x03;
    bytes[0x4A] = 0x00;
    bytes[0x4B] = 0x10;
    bytes[0x4C] = 0x4E;  // 0x104C: blr
    bytes[0x4D] = 0x80;
    bytes[0x4E] = 0x00;
    bytes[0x4F] = 0x20;

    return bytes;
  }();

  auto binary = MakeBinaryView(0x1000, kDynamicCodePointerStore);
  rex::codegen::RecompilerConfig config;
  auto ctx = rex::codegen::CodegenContext::Create(std::move(binary), std::move(config));
  ctx.initDecoded();

  ctx.scan.codeRegions.push_back({0x1000, 0x1050});
  ctx.scan.pdataSizes[0x1000] = 0x20;
  ctx.scan.pdataSizes[0x1040] = 0x10;
  ctx.graph.addFunction(0x1000, 0x20, rex::codegen::FunctionAuthority::PDATA, true);
  ctx.graph.addFunction(0x1040, 0x10, rex::codegen::FunctionAuthority::PDATA, true);

  auto result = rex::codegen::phases::Discover(ctx);
  REQUIRE(result.has_value());
  CHECK(ctx.graph.isEntryPoint(0x1018));
}

TEST_CASE("Discover registers code-pointer arguments passed to helper calls",
          "[codegen][Discover][FunctionPointer]") {
  auto kHelperCallbackPointer = [] {
    std::array<uint8_t, 0x44> bytes{};

    bytes[0x00] = 0x3D;  // 0x1000: lis r11,0
    bytes[0x01] = 0x60;
    bytes[0x02] = 0x00;
    bytes[0x03] = 0x00;
    bytes[0x04] = 0x38;  // 0x1004: addi r5,r11,0x1020
    bytes[0x05] = 0xAB;
    bytes[0x06] = 0x10;
    bytes[0x07] = 0x20;
    bytes[0x08] = 0x48;  // 0x1008: bl 0x1040
    bytes[0x09] = 0x00;
    bytes[0x0A] = 0x00;
    bytes[0x0B] = 0x39;
    bytes[0x0C] = 0x4E;  // 0x100C: blr
    bytes[0x0D] = 0x80;
    bytes[0x0E] = 0x00;
    bytes[0x0F] = 0x20;

    bytes[0x20] = 0x3D;  // 0x1020: lis r11,0
    bytes[0x21] = 0x60;
    bytes[0x22] = 0x00;
    bytes[0x23] = 0x00;
    bytes[0x24] = 0x38;  // 0x1024: li r5,0
    bytes[0x25] = 0xA0;
    bytes[0x26] = 0x00;
    bytes[0x27] = 0x00;
    bytes[0x28] = 0x38;  // 0x1028: addi r4,r11,0x2000
    bytes[0x29] = 0x8B;
    bytes[0x2A] = 0x20;
    bytes[0x2B] = 0x00;
    bytes[0x2C] = 0x48;  // 0x102C: b 0x1040
    bytes[0x2D] = 0x00;
    bytes[0x2E] = 0x00;
    bytes[0x2F] = 0x14;

    bytes[0x30] = 0x38;  // 0x1030: li r3,2
    bytes[0x31] = 0x60;
    bytes[0x32] = 0x00;
    bytes[0x33] = 0x02;
    bytes[0x34] = 0x4E;  // 0x1034: blr
    bytes[0x35] = 0x80;
    bytes[0x36] = 0x00;
    bytes[0x37] = 0x20;

    bytes[0x40] = 0x4E;  // 0x1040: blr
    bytes[0x41] = 0x80;
    bytes[0x42] = 0x00;
    bytes[0x43] = 0x20;

    return bytes;
  }();

  auto binary = MakeBinaryView(0x1000, kHelperCallbackPointer);
  rex::codegen::RecompilerConfig config;
  auto ctx = rex::codegen::CodegenContext::Create(std::move(binary), std::move(config));
  ctx.initDecoded();

  ctx.scan.codeRegions.push_back({0x1000, 0x1044});
  ctx.scan.pdataSizes[0x1000] = 0x10;
  ctx.scan.pdataSizes[0x1030] = 0x08;
  ctx.scan.pdataSizes[0x1040] = 0x04;
  ctx.graph.addFunction(0x1000, 0x10, rex::codegen::FunctionAuthority::PDATA, true);
  ctx.graph.addFunction(0x1030, 0x08, rex::codegen::FunctionAuthority::PDATA, true);
  ctx.graph.addFunction(0x1040, 0x04, rex::codegen::FunctionAuthority::PDATA, true);

  auto result = rex::codegen::phases::Discover(ctx);
  REQUIRE(result.has_value());
  CHECK(ctx.graph.isEntryPoint(0x1020));
}

TEST_CASE("Discover follows fall-through after conditional bcctr",
          "[codegen][Discover][ControlFlow]") {
  constexpr std::array<uint8_t, 28> kConditionalCtrBranch = {
      0x3C, 0x80, 0x83, 0x09,  // 0x1000: lis r4,0x8309
      0x80, 0x04, 0x2C, 0xC0,  // 0x1004: lwz r0,0x2CC0(r4)
      0x2C, 0x00, 0x00, 0x00,  // 0x1008: cmpwi r0,0
      0x7C, 0x09, 0x03, 0xA6,  // 0x100C: mtctr r0
      0x4C, 0x82, 0x04, 0x20,  // 0x1010: bnectr
      0x7C, 0x08, 0x02, 0xA6,  // 0x1014: mflr r0
      0x4E, 0x80, 0x00, 0x20,  // 0x1018: blr
  };

  auto binary = MakeBinaryView(0x1000, kConditionalCtrBranch);
  rex::codegen::RecompilerConfig config;
  auto ctx = rex::codegen::CodegenContext::Create(std::move(binary), std::move(config));
  ctx.initDecoded();

  ctx.scan.codeRegions.push_back({0x1000, 0x101C});
  ctx.scan.pdataSizes[0x1000] = 0x1C;
  ctx.graph.addFunction(0x1000, 0x1C, rex::codegen::FunctionAuthority::PDATA, true);

  auto result = rex::codegen::phases::Discover(ctx);
  REQUIRE(result.has_value());

  const auto* node = ctx.graph.getFunction(0x1000);
  REQUIRE(node != nullptr);
  REQUIRE(node->instructions().size() == 7);
  REQUIRE(node->blocks().size() == 2);
  CHECK(node->blocks()[1].base == 0x1014);
  CHECK(node->isLabel(0x1014));
  CHECK(node->containsAddress(0x1014));
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
