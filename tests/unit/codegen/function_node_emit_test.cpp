#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#define _ALLOW_KEYWORD_MACROS
#define private public
#include <rex/codegen/binary_view.h>
#undef private

#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/code_emitter.h>
#include <rex/codegen/config.h>
#include <rex/codegen/function_graph.h>

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

size_t RequireTokenOrder(std::string_view text, std::string_view first, std::string_view second) {
  const size_t firstPos = text.find(first);
  REQUIRE(firstPos != std::string_view::npos);
  const size_t secondPos = text.find(second, firstPos + first.size());
  REQUIRE(secondPos != std::string_view::npos);
  return secondPos;
}

size_t CountOccurrences(std::string_view text, std::string_view needle) {
  size_t count = 0;
  size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string_view::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

}  // namespace

TEST_CASE("FunctionNode emit keeps edge-less entry targets local when owned by the function",
          "[codegen][FunctionNode]") {
  // 0x48000020 = b 0x1020 from 0x1000. The graph knows 0x1020 as an entry
  // point, but this emitting function also owns the target as an internal
  // block. Without an explicit call edge at the branch site, emit it locally.
  constexpr std::array<uint8_t, 4> kBranchToOwnedEntryPoint = {0x48, 0x00, 0x00, 0x20};

  auto binary = MakeBinaryView(0x1000, kBranchToOwnedEntryPoint);
  rex::codegen::RecompilerConfig config;
  rex::codegen::FunctionGraph graph;
  auto* node = graph.addFunction(0x1000, 0x40, rex::codegen::FunctionAuthority::PDATA, true);
  REQUIRE(node != nullptr);
  graph.addFunction(0x1020, 4, rex::codegen::FunctionAuthority::DISCOVERED, true);
  node->discover({rex::codegen::Block{.base = 0x1000, .size = 4},
                  rex::codegen::Block{.base = 0x1020, .size = 4}},
                 {}, {0x1020});
  node->seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node->emitCpp(ctx);
  CHECK(cpp.find("goto loc_1020;") != std::string::npos);
  CHECK(cpp.find("Unresolved call from 0x00001000 to 0x00001020") == std::string::npos);
}

TEST_CASE("FunctionNode emit keeps decrement branches local in owned pre-entry blocks",
          "[codegen][FunctionNode]") {
  std::array<uint8_t, 0x60> bytes{};
  for (size_t offset = 0; offset < bytes.size(); offset += 4) {
    bytes[offset + 0] = 0x60;
    bytes[offset + 1] = 0x00;
    bytes[offset + 2] = 0x00;
    bytes[offset + 3] = 0x00;
  }
  bytes[0x54] = 0x42;
  bytes[0x55] = 0x00;
  bytes[0x56] = 0xFF;
  bytes[0x57] = 0xAC;  // 0x1078: bdnz 0x1024
  bytes[0x5C] = 0x4E;
  bytes[0x5D] = 0x80;
  bytes[0x5E] = 0x00;
  bytes[0x5F] = 0x20;  // 0x1080: blr

  auto binary = MakeBinaryView(0x1024, bytes);
  rex::codegen::RecompilerConfig config;
  rex::codegen::FunctionGraph graph;
  auto* node = graph.addFunction(0x1080, 4, rex::codegen::FunctionAuthority::DISCOVERED, true);
  REQUIRE(node != nullptr);
  node->discover({rex::codegen::Block{.base = 0x1024, .size = static_cast<uint32_t>(bytes.size())}},
                 {}, {0x1024, 0x1078, 0x1080});
  node->seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node->emitCpp(ctx);
  CHECK(cpp.find("if (rex_branch_taken_00001078) goto loc_1024;") != std::string::npos);
  CHECK(cpp.find("branch to 0x1024 outside function") == std::string::npos);
}

TEST_CASE("FunctionNode emit profiles local conditional branch outcomes",
          "[codegen][FunctionNode]") {
  std::array<uint8_t, 0x18> bytes{};
  bytes[0x00] = 0x2C;
  bytes[0x01] = 0x03;
  bytes[0x02] = 0x00;
  bytes[0x03] = 0x00;  // 0x1000: cmpwi r3,0
  bytes[0x04] = 0x41;
  bytes[0x05] = 0x82;
  bytes[0x06] = 0x00;
  bytes[0x07] = 0x0C;  // 0x1004: beq 0x1010
  bytes[0x08] = 0x38;
  bytes[0x09] = 0x60;
  bytes[0x0A] = 0x00;
  bytes[0x0B] = 0x01;  // 0x1008: li r3,1
  bytes[0x0C] = 0x4E;
  bytes[0x0D] = 0x80;
  bytes[0x0E] = 0x00;
  bytes[0x0F] = 0x20;  // 0x100C: blr
  bytes[0x10] = 0x38;
  bytes[0x11] = 0x60;
  bytes[0x12] = 0x00;
  bytes[0x13] = 0x00;  // 0x1010: li r3,0
  bytes[0x14] = 0x4E;
  bytes[0x15] = 0x80;
  bytes[0x16] = 0x00;
  bytes[0x17] = 0x20;  // 0x1014: blr

  auto binary = MakeBinaryView(0x1000, bytes);
  rex::codegen::RecompilerConfig config;
  rex::codegen::FunctionGraph graph;
  auto* node = graph.addFunction(0x1000, 0x18, rex::codegen::FunctionAuthority::PDATA, true);
  REQUIRE(node != nullptr);
  node->discover({rex::codegen::Block{.base = 0x1000, .size = 0x10},
                  rex::codegen::Block{.base = 0x1010, .size = 0x08}},
                 {}, {0x1000, 0x1008, 0x1010});
  node->seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node->emitCpp(ctx);
  CHECK(cpp.find("#if defined(REXGLUE_PROFILE_GUEST_CONDITIONAL_BRANCHES)") != std::string::npos);
  CHECK(cpp.find("const bool rex_branch_taken_00001004 = ctx.cr0.eq;") != std::string::npos);
  CHECK(cpp.find("PROFILE_GUEST_CONDITIONAL_BRANCH_OUTCOME(0x00001000, \"sub_00001000\", "
                 "0x00001004, 0x00001010, rex_branch_taken_00001004);") != std::string::npos);
  CHECK(cpp.find("if (rex_branch_taken_00001004) goto loc_00001010;") != std::string::npos);
  CHECK(cpp.find("#else") != std::string::npos);
  CHECK(cpp.find("if (ctx.cr0.eq) goto loc_00001010;") != std::string::npos);
  CHECK(cpp.find("#endif") != std::string::npos);
  RequireTokenOrder(cpp, "#if defined(REXGLUE_PROFILE_GUEST_CONDITIONAL_BRANCHES)",
                    "PROFILE_GUEST_CONDITIONAL_BRANCH_OUTCOME(0x00001000, \"sub_00001000\", ");
  RequireTokenOrder(cpp, "#else", "if (ctx.cr0.eq) goto loc_00001010;");
}

TEST_CASE("FunctionNode emit profiles bounded decrement branch outcomes",
          "[codegen][FunctionNode]") {
  std::array<uint8_t, 0x60> bytes{};
  for (size_t offset = 0; offset < bytes.size(); offset += 4) {
    bytes[offset + 0] = 0x60;
    bytes[offset + 1] = 0x00;
    bytes[offset + 2] = 0x00;
    bytes[offset + 3] = 0x00;
  }
  bytes[0x54] = 0x42;
  bytes[0x55] = 0x00;
  bytes[0x56] = 0xFF;
  bytes[0x57] = 0xAC;  // 0x1078: bdnz 0x1024
  bytes[0x5C] = 0x4E;
  bytes[0x5D] = 0x80;
  bytes[0x5E] = 0x00;
  bytes[0x5F] = 0x20;  // 0x1080: blr

  auto binary = MakeBinaryView(0x1024, bytes);
  rex::codegen::RecompilerConfig config;
  rex::codegen::FunctionGraph graph;
  auto* node = graph.addFunction(0x1080, 4, rex::codegen::FunctionAuthority::DISCOVERED, true);
  REQUIRE(node != nullptr);
  node->discover({rex::codegen::Block{.base = 0x1024, .size = static_cast<uint32_t>(bytes.size())}},
                 {}, {0x1024, 0x1078, 0x1080});
  node->seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node->emitCpp(ctx);
  CHECK(cpp.find("#if defined(REXGLUE_PROFILE_GUEST_CONDITIONAL_BRANCHES)") != std::string::npos);
  CHECK(cpp.find("const bool rex_branch_taken_00001078 = ctx.ctr.u32 != 0;") != std::string::npos);
  CHECK(cpp.find("PROFILE_GUEST_CONDITIONAL_BRANCH_OUTCOME(0x00001080, \"sub_00001080\", "
                 "0x00001078, 0x00001024, rex_branch_taken_00001078);") != std::string::npos);
  CHECK(cpp.find("if (rex_branch_taken_00001078) goto loc_1024;") != std::string::npos);
  CHECK(cpp.find("#else") != std::string::npos);
  CHECK(cpp.find("if (ctx.ctr.u32 != 0) goto loc_1024;") != std::string::npos);
  CHECK(cpp.find("#endif") != std::string::npos);
  RequireTokenOrder(cpp, "#else", "if (ctx.ctr.u32 != 0) goto loc_1024;");
}
TEST_CASE("FunctionNode emit starts promoted alternate entries at the entry label",
          "[codegen][FunctionNode]") {
  std::array<uint8_t, 0x68> bytes{};
  for (size_t offset = 0; offset < bytes.size(); offset += 4) {
    bytes[offset + 0] = 0x60;
    bytes[offset + 1] = 0x00;
    bytes[offset + 2] = 0x00;
    bytes[offset + 3] = 0x00;
  }
  bytes[0x00] = 0x38;
  bytes[0x01] = 0x60;
  bytes[0x02] = 0x00;
  bytes[0x03] = 0x01;  // 0x1020: li r3,1
  bytes[0x04] = 0x4E;
  bytes[0x05] = 0x80;
  bytes[0x06] = 0x00;
  bytes[0x07] = 0x20;  // 0x1024: blr
  bytes[0x60] = 0x38;
  bytes[0x61] = 0x60;
  bytes[0x62] = 0x00;
  bytes[0x63] = 0x02;  // 0x1080: li r3,2
  bytes[0x64] = 0x4E;
  bytes[0x65] = 0x80;
  bytes[0x66] = 0x00;
  bytes[0x67] = 0x20;  // 0x1084: blr

  auto binary = MakeBinaryView(0x1020, bytes);
  rex::codegen::RecompilerConfig config;
  rex::codegen::FunctionGraph graph;
  auto* node = graph.addFunction(0x1080, 8, rex::codegen::FunctionAuthority::DISCOVERED, true);
  REQUIRE(node != nullptr);
  node->discover({rex::codegen::Block{.base = 0x1020, .size = 8},
                  rex::codegen::Block{.base = 0x1080, .size = 8}},
                 {}, {0x1020, 0x1080});
  node->seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node->emitCpp(ctx);
  RequireTokenOrder(cpp, "REX_FUNC_PROLOGUE();", "\tgoto loc_1080;");
  RequireTokenOrder(cpp, "\tgoto loc_1080;", "loc_1020:");
  CHECK(cpp.find("loc_1080:") != std::string::npos);
}

TEST_CASE("FunctionNode emit profiles generated guest function addresses",
          "[codegen][FunctionNode][perf]") {
  constexpr std::array<uint8_t, 8> kReturn = {
      0x7F, 0xFF, 0xFB, 0x78,  // db16cyc
      0x4E, 0x80, 0x00, 0x20,  // blr
  };

  auto binary = MakeBinaryView(0x1000, kReturn);
  rex::codegen::RecompilerConfig config;
  rex::codegen::FunctionGraph graph;
  rex::codegen::FunctionNode node(0x1000, 8, rex::codegen::FunctionAuthority::CONFIG);
  node.discover({rex::codegen::Block{.base = 0x1000, .size = 8}}, {}, {});
  node.seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node.emitCpp(ctx);
  RequireTokenOrder(cpp, "REX_FUNC_PROLOGUE();",
                    "\tREX_PROFILE_GUEST_FUNCTION_SCOPE(0x00001000, \"sub_00001000\", 1);");
}

TEST_CASE("FunctionNode emit skips guest function profiling around longjmp helpers",
          "[codegen][FunctionNode][perf]") {
  constexpr std::array<uint8_t, 4> kReturn = {
      0x4E, 0x80, 0x00, 0x20,  // blr
  };

  auto binary = MakeBinaryView(0x1000, kReturn);
  rex::codegen::RecompilerConfig config;
  config.setJmpAddress = 0x2000;
  config.longJmpAddress = 0x3000;
  rex::codegen::FunctionGraph graph;
  rex::codegen::FunctionNode node(0x1000, 4, rex::codegen::FunctionAuthority::CONFIG);
  node.discover({rex::codegen::Block{.base = 0x1000, .size = 4}}, {}, {});
  node.seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node.emitCpp(ctx);
  CHECK(cpp.find("REX_PROFILE_GUEST_FUNCTION_SCOPE(") == std::string::npos);
}

TEST_CASE("FunctionNode emit skips native import profiling around longjmp helpers",
          "[codegen][FunctionNode][perf]") {
  constexpr std::array<uint8_t, 8> kImportCall = {
      0x48, 0x00, 0x10, 0x01,  // 0x1000: bl 0x2000
      0x4E, 0x80, 0x00, 0x20,  // 0x1004: blr
  };

  auto binary = MakeBinaryView(0x1000, kImportCall);
  rex::codegen::RecompilerConfig config;
  config.setJmpAddress = 0x3000;
  config.longJmpAddress = 0x4000;
  rex::codegen::FunctionGraph graph;
  auto* node = graph.addFunction(0x1000, 8, rex::codegen::FunctionAuthority::CONFIG, true);
  REQUIRE(node != nullptr);
  node->discover({rex::codegen::Block{.base = 0x1000, .size = 8}}, {}, {});
  graph.addCallToFunction(
      0x1000, 0x1000, rex::codegen::CallTarget::import(0x2000, "__imp__KeDelayExecutionThread"));
  node->seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node->emitCpp(ctx);
  CHECK(cpp.find("REX_CALL_NATIVE_FUNC(") == std::string::npos);
  CHECK(cpp.find("__imp__KeDelayExecutionThread(ctx, base);") != std::string::npos);
}

TEST_CASE("FunctionNode emit wraps direct import calls for native profiling",
          "[codegen][FunctionNode][perf]") {
  constexpr std::array<uint8_t, 8> kImportCall = {
      0x48, 0x00, 0x10, 0x01,  // 0x1000: bl 0x2000
      0x4E, 0x80, 0x00, 0x20,  // 0x1004: blr
  };

  auto binary = MakeBinaryView(0x1000, kImportCall);
  rex::codegen::RecompilerConfig config;
  rex::codegen::FunctionGraph graph;
  auto* node = graph.addFunction(0x1000, 8, rex::codegen::FunctionAuthority::CONFIG, true);
  REQUIRE(node != nullptr);
  node->discover({rex::codegen::Block{.base = 0x1000, .size = 8}}, {}, {});
  graph.addCallToFunction(
      0x1000, 0x1000, rex::codegen::CallTarget::import(0x2000, "__imp__KeDelayExecutionThread"));
  node->seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node->emitCpp(ctx);
  CHECK(cpp.find("REX_CALL_NATIVE_FUNC(0x00002000, \"__imp__KeDelayExecutionThread\", "
                 "__imp__KeDelayExecutionThread);") != std::string::npos);
  CHECK(cpp.find("__imp__KeDelayExecutionThread(ctx, base);") == std::string::npos);
}

TEST_CASE("FunctionNode emit wraps import nodes resolved as function targets for native profiling",
          "[codegen][FunctionNode][perf]") {
  constexpr std::array<uint8_t, 8> kImportCall = {
      0x48, 0x00, 0x10, 0x01,  // 0x1000: bl 0x2000
      0x4E, 0x80, 0x00, 0x20,  // 0x1004: blr
  };

  auto binary = MakeBinaryView(0x1000, kImportCall);
  rex::codegen::RecompilerConfig config;
  rex::codegen::FunctionGraph graph;
  auto* node = graph.addFunction(0x1000, 8, rex::codegen::FunctionAuthority::CONFIG, true);
  REQUIRE(node != nullptr);
  auto* importNode = graph.addImportFunction(0x2000, "__imp__NtWaitForSingleObjectEx");
  REQUIRE(importNode != nullptr);
  node->discover({rex::codegen::Block{.base = 0x1000, .size = 8}}, {}, {});
  graph.addCallToFunction(0x1000, 0x1000, rex::codegen::CallTarget::function(importNode));
  node->seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node->emitCpp(ctx);
  CHECK(cpp.find("REX_CALL_NATIVE_FUNC(0x00002000, \"__imp__NtWaitForSingleObjectEx\", "
                 "__imp__NtWaitForSingleObjectEx);") != std::string::npos);
  CHECK(cpp.find("__imp__NtWaitForSingleObjectEx(ctx, base);") == std::string::npos);
}

TEST_CASE("FunctionNode emit attributes resolved direct guest calls for profiling",
          "[codegen][FunctionNode][perf]") {
  constexpr std::array<uint8_t, 8> kDirectCall = {
      0x48, 0x00, 0x10, 0x01,  // 0x1000: bl 0x2000
      0x4E, 0x80, 0x00, 0x20,  // 0x1004: blr
  };

  auto binary = MakeBinaryView(0x1000, kDirectCall);
  rex::codegen::RecompilerConfig config;
  rex::codegen::FunctionGraph graph;
  auto* caller = graph.addFunction(0x1000, 8, rex::codegen::FunctionAuthority::CONFIG, true);
  auto* callee = graph.addFunction(0x2000, 4, rex::codegen::FunctionAuthority::CONFIG, true);
  REQUIRE(caller != nullptr);
  REQUIRE(callee != nullptr);
  caller->discover({rex::codegen::Block{.base = 0x1000, .size = 8}}, {}, {});
  callee->discover({rex::codegen::Block{.base = 0x2000, .size = 4}}, {}, {});
  graph.addCallToFunction(0x1000, 0x1000, rex::codegen::CallTarget::function(callee));
  caller->seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = caller->emitCpp(ctx);
  const std::string direct_call =
      "REX_CALL_DIRECT_FUNC_AT(0x00001000, \"sub_00001000\", 0x00001000, "
      "0x00002000, \"sub_00002000\", sub_00002000);";
  CHECK(cpp.find(direct_call) != std::string::npos);
  CHECK(cpp.find("\tsub_00002000(ctx, base);") == std::string::npos);
  RequireTokenOrder(cpp, "ctx.lr = 0x1004;", direct_call);
}

TEST_CASE("FunctionNode emit excludes register save helpers from direct call profiling",
          "[codegen][FunctionNode][perf]") {
  constexpr std::array<uint8_t, 8> kDirectCall = {
      0x48, 0x00, 0x10, 0x01,  // 0x1000: bl 0x2000
      0x4E, 0x80, 0x00, 0x20,  // 0x1004: blr
  };

  auto binary = MakeBinaryView(0x1000, kDirectCall);
  rex::codegen::RecompilerConfig config;
  rex::codegen::FunctionGraph graph;
  auto* caller = graph.addFunction(0x1000, 8, rex::codegen::FunctionAuthority::CONFIG, true);
  auto* helper = graph.addFunction(0x2000, 4, rex::codegen::FunctionAuthority::HELPER, true);
  REQUIRE(caller != nullptr);
  REQUIRE(helper != nullptr);
  helper->setName("__savegprlr_29");
  caller->discover({rex::codegen::Block{.base = 0x1000, .size = 8}}, {}, {});
  helper->discover({rex::codegen::Block{.base = 0x2000, .size = 4}}, {}, {});
  graph.addCallToFunction(0x1000, 0x1000, rex::codegen::CallTarget::function(helper));
  caller->seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = caller->emitCpp(ctx);
  CHECK(cpp.find("\t__savegprlr_29(ctx, base);") != std::string::npos);
  CHECK(cpp.find("REX_CALL_DIRECT_FUNC_AT(0x00001000, \"sub_00001000\", 0x00001000, "
                 "0x00002000, \"__savegprlr_29\", __savegprlr_29);") == std::string::npos);
}

TEST_CASE("FunctionNode emit handles conditional branch-to-CTR-and-link",
          "[codegen][FunctionNode]") {
  // 0x4C820421 = bnectrl cr0. The call is conditional and must preserve
  // fall-through when CR0 EQ is set.
  constexpr std::array<uint8_t, 8> kConditionalCtrLink = {
      0x4C, 0x82, 0x04, 0x21,  // bnectrl cr0
      0x4E, 0x80, 0x00, 0x20,  // blr
  };

  auto binary = MakeBinaryView(0x1000, kConditionalCtrLink);
  rex::codegen::RecompilerConfig config;
  rex::codegen::FunctionGraph graph;
  rex::codegen::FunctionNode node(0x1000, 8, rex::codegen::FunctionAuthority::CONFIG);
  node.discover({rex::codegen::Block{.base = 0x1000, .size = 8}}, {}, {});
  node.seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node.emitCpp(ctx);
  CHECK(cpp.find("if (!ctx.cr0.eq) {") != std::string::npos);
  CHECK(cpp.find("ctx.lr = 0x1004;") != std::string::npos);
  RequireTokenOrder(cpp, "ctx.lr = 0x1004;", "if (!ctx.cr0.eq) {");
  CHECK(cpp.find("REX_CALL_INDIRECT_FUNC_AT(0x00001000, \"sub_00001000\", 0x00001000, "
                 "ctx.ctr.u32);") != std::string::npos);
  CHECK(cpp.find("UNIMPLEMENTED") == std::string::npos);
}

TEST_CASE("FunctionNode emit handles raw branch-to-register BO/BI forms",
          "[codegen][FunctionNode]") {
  auto emit = [](std::array<uint8_t, 8> bytes) {
    auto binary = MakeBinaryView(0x1000, bytes);
    rex::codegen::RecompilerConfig config;
    rex::codegen::FunctionGraph graph;
    rex::codegen::FunctionNode node(0x1000, 8, rex::codegen::FunctionAuthority::CONFIG);
    node.discover({rex::codegen::Block{.base = 0x1000, .size = 8}}, {}, {});
    node.seal();

    rex::codegen::EmitContext ctx{
        .binary = binary,
        .config = config,
        .graph = graph,
        .entryPoint = 0,
        .resolver = nullptr,
    };

    return node.emitCpp(ctx);
  };

  constexpr std::array<uint8_t, 8> kRawConditionalCtrLink = {
      0x4C, 0x02, 0x04, 0x21,  // bcctrl 0,2,0
      0x4E, 0x80, 0x00, 0x20,  // blr
  };
  const std::string ctr_link_cpp = emit(kRawConditionalCtrLink);
  CHECK(ctr_link_cpp.find("if (!ctx.cr0.eq) {") != std::string::npos);
  CHECK(ctr_link_cpp.find("ctx.lr = 0x1004;") != std::string::npos);
  RequireTokenOrder(ctr_link_cpp, "ctx.lr = 0x1004;", "if (!ctx.cr0.eq) {");
  CHECK(ctr_link_cpp.find("REX_CALL_INDIRECT_FUNC_AT(0x00001000, \"sub_00001000\", "
                          "0x00001000, ctx.ctr.u32);") != std::string::npos);
  CHECK(ctr_link_cpp.find("UNIMPLEMENTED") == std::string::npos);

  constexpr std::array<uint8_t, 8> kRawConditionalLrLink = {
      0x4C, 0x02, 0x00, 0x21,  // bclrl 0,2,0
      0x4E, 0x80, 0x00, 0x20,  // blr
  };
  const std::string lr_link_cpp = emit(kRawConditionalLrLink);
  CHECK(lr_link_cpp.find("--ctx.ctr.u64;") != std::string::npos);
  CHECK(lr_link_cpp.find("ctx.ctr.u32 != 0 && !ctx.cr0.eq") != std::string::npos);
  CHECK(lr_link_cpp.find("auto old_lr = ctx.lr;") != std::string::npos);
  CHECK(lr_link_cpp.find("ctx.lr = 0x1004;") != std::string::npos);
  RequireTokenOrder(lr_link_cpp, "auto old_lr = ctx.lr;", "ctx.lr = 0x1004;");
  CHECK(lr_link_cpp.find("REX_CALL_INDIRECT_FUNC_AT(0x00001000, \"sub_00001000\", "
                         "0x00001000, uint32_t(old_lr));") != std::string::npos);
  CHECK(lr_link_cpp.find("UNIMPLEMENTED") == std::string::npos);
}

TEST_CASE("FunctionNode emit handles linked branch-to-LR aliases", "[codegen][FunctionNode]") {
  auto emit = [](std::array<uint8_t, 8> bytes) {
    auto binary = MakeBinaryView(0x1000, bytes);
    rex::codegen::RecompilerConfig config;
    rex::codegen::FunctionGraph graph;
    rex::codegen::FunctionNode node(0x1000, 8, rex::codegen::FunctionAuthority::CONFIG);
    node.discover({rex::codegen::Block{.base = 0x1000, .size = 8}}, {}, {});
    node.seal();

    rex::codegen::EmitContext ctx{
        .binary = binary,
        .config = config,
        .graph = graph,
        .entryPoint = 0,
        .resolver = nullptr,
    };

    return node.emitCpp(ctx);
  };

  constexpr std::array<uint8_t, 8> kNeLrLink = {
      0x4C, 0x82, 0x00, 0x21,  // bnelrl cr0
      0x4E, 0x80, 0x00, 0x20,  // blr
  };
  const std::string ne_cpp = emit(kNeLrLink);
  CHECK(ne_cpp.find("if (!ctx.cr0.eq) {") != std::string::npos);
  CHECK(ne_cpp.find("auto old_lr = ctx.lr;") != std::string::npos);
  CHECK(ne_cpp.find("ctx.lr = 0x1004;") != std::string::npos);
  RequireTokenOrder(ne_cpp, "auto old_lr = ctx.lr;", "ctx.lr = 0x1004;");
  CHECK(ne_cpp.find("REX_CALL_INDIRECT_FUNC_AT(0x00001000, \"sub_00001000\", 0x00001000, "
                    "uint32_t(old_lr));") != std::string::npos);
  CHECK(ne_cpp.find("UNIMPLEMENTED") == std::string::npos);

  constexpr std::array<uint8_t, 8> kEqLrLink = {
      0x4D, 0x82, 0x00, 0x21,  // beqlrl cr0
      0x4E, 0x80, 0x00, 0x20,  // blr
  };
  const std::string eq_cpp = emit(kEqLrLink);
  CHECK(eq_cpp.find("if (ctx.cr0.eq) {") != std::string::npos);
  CHECK(eq_cpp.find("auto old_lr = ctx.lr;") != std::string::npos);
  CHECK(eq_cpp.find("ctx.lr = 0x1004;") != std::string::npos);
  RequireTokenOrder(eq_cpp, "auto old_lr = ctx.lr;", "ctx.lr = 0x1004;");
  CHECK(eq_cpp.find("REX_CALL_INDIRECT_FUNC_AT(0x00001000, \"sub_00001000\", 0x00001000, "
                    "uint32_t(old_lr));") != std::string::npos);
  CHECK(eq_cpp.find("UNIMPLEMENTED") == std::string::npos);
}

TEST_CASE("FunctionNode emit lowers db16cyc spin hints to host pause hints",
          "[codegen][FunctionNode]") {
  constexpr std::array<uint8_t, 4> kDb16cyc = {
      0x7F, 0xFF, 0xFB, 0x78,  // db16cyc
  };

  auto binary = MakeBinaryView(0x1000, kDb16cyc);
  rex::codegen::RecompilerConfig config;
  rex::codegen::FunctionGraph graph;
  rex::codegen::FunctionNode node(0x1000, 4, rex::codegen::FunctionAuthority::CONFIG);
  node.discover({rex::codegen::Block{.base = 0x1000, .size = 4}}, {}, {});
  node.seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node.emitCpp(ctx);
  CHECK(cpp.find("// db16cyc \n\tREX_PROFILE_GUEST_SPIN_HINT_EXECUTION();\n"
                 "\trex::ppc_delay_execution_hint();") != std::string::npos);
}

TEST_CASE("FunctionNode emit batches consecutive db16cyc spin hints", "[codegen][FunctionNode]") {
  constexpr std::array<uint8_t, 16> kConsecutiveDb16cyc = {
      0x7F, 0xFF, 0xFB, 0x78,  // db16cyc
      0x7F, 0xFF, 0xFB, 0x78,  // db16cyc
      0x7F, 0xFF, 0xFB, 0x78,  // db16cyc
      0x4E, 0x80, 0x00, 0x20,  // blr
  };

  auto binary = MakeBinaryView(0x1000, kConsecutiveDb16cyc);
  rex::codegen::RecompilerConfig config;
  rex::codegen::FunctionGraph graph;
  rex::codegen::FunctionNode node(0x1000, 16, rex::codegen::FunctionAuthority::CONFIG);
  node.discover({rex::codegen::Block{.base = 0x1000, .size = 16}}, {}, {});
  node.seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node.emitCpp(ctx);
  CHECK(cpp.find("\tREX_PROFILE_GUEST_FUNCTION_SCOPE(0x00001000, \"sub_00001000\", 3);") !=
        std::string::npos);
  CHECK(cpp.find("// db16cyc x3\n\tREX_PROFILE_GUEST_SPIN_HINT_EXECUTIONS(3);\n"
                 "\trex::ppc_delay_execution_hints(3);") != std::string::npos);
  CHECK(CountOccurrences(cpp, "REX_PROFILE_GUEST_SPIN_HINT_EXECUTION();") == 0);
  CHECK(CountOccurrences(cpp, "rex::ppc_delay_execution_hint();") == 0);
}

TEST_CASE("FunctionNode emit does not batch db16cyc across labels", "[codegen][FunctionNode]") {
  constexpr std::array<uint8_t, 12> kLabeledDb16cyc = {
      0x7F, 0xFF, 0xFB, 0x78,  // db16cyc
      0x7F, 0xFF, 0xFB, 0x78,  // db16cyc
      0x4E, 0x80, 0x00, 0x20,  // blr
  };

  auto binary = MakeBinaryView(0x1000, kLabeledDb16cyc);
  rex::codegen::RecompilerConfig config;
  rex::codegen::FunctionGraph graph;
  rex::codegen::FunctionNode node(0x1000, 12, rex::codegen::FunctionAuthority::CONFIG);
  node.discover({rex::codegen::Block{.base = 0x1000, .size = 12}}, {}, {0x1004});
  node.seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node.emitCpp(ctx);
  CHECK(cpp.find("loc_1004:") != std::string::npos);
  CHECK(CountOccurrences(cpp, "REX_PROFILE_GUEST_SPIN_HINT_EXECUTION();") == 2);
  CHECK(CountOccurrences(cpp, "rex::ppc_delay_execution_hint();") == 2);
  CHECK(cpp.find("REX_PROFILE_GUEST_SPIN_HINT_EXECUTIONS(2);") == std::string::npos);
  CHECK(cpp.find("rex::ppc_delay_execution_hints(2);") == std::string::npos);
}

TEST_CASE("FunctionNode emit falls back to CTR when jump-table index is out of range",
          "[codegen][FunctionNode]") {
  constexpr std::array<uint8_t, 8> kBctrWithLocalTarget = {
      0x4E, 0x80, 0x04, 0x20,  // bctr
      0x4E, 0x80, 0x00, 0x20,  // blr
  };

  auto binary = MakeBinaryView(0x1000, kBctrWithLocalTarget);
  rex::codegen::RecompilerConfig config;
  rex::codegen::FunctionGraph graph;
  auto* node = graph.addFunction(0x1000, 8, rex::codegen::FunctionAuthority::CONFIG, true);
  REQUIRE(node != nullptr);
  node->discover({rex::codegen::Block{.base = 0x1000, .size = 4},
                  rex::codegen::Block{.base = 0x1004, .size = 4}},
                 {}, {0x1004});
  graph.addJumpTableToFunction(0x1000, rex::codegen::JumpTable{
                                           .bctrAddress = 0x1000,
                                           .tableAddress = 0x2000,
                                           .indexRegister = 3,
                                           .targets = {0x1004},
                                       });
  node->seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node->emitCpp(ctx);
  CHECK(cpp.find("switch (ctx.r3.u32)") != std::string::npos);
  CHECK(cpp.find("case 0:") != std::string::npos);
  CHECK(cpp.find("goto loc_1004;") != std::string::npos);
  CHECK(cpp.find("default:") != std::string::npos);
  CHECK(cpp.find("REX_CALL_INDIRECT_FUNC_AT(0x00001000, \"sub_00001000\", 0x00001000, "
                 "ctx.ctr.u32);") != std::string::npos);
  CHECK(cpp.find("__builtin_trap(); // Switch case out of range") == std::string::npos);
}

TEST_CASE("FunctionNode SEH catch uses captured establisher frame",
          "[codegen][FunctionNode][SEH]") {
  constexpr std::array<uint8_t, 12> kFrameSetupAndReturn = {
      0x3B, 0xE1, 0xFE, 0xC0,  // addi r31,r1,-320
      0x94, 0x21, 0xFE, 0xC0,  // stwu r1,-320(r1)
      0x4E, 0x80, 0x00, 0x20,  // blr
  };

  auto binary = MakeBinaryView(0x1000, kFrameSetupAndReturn);
  rex::codegen::RecompilerConfig config;
  config.generateExceptionHandlers = true;

  rex::codegen::FunctionGraph graph;
  auto* restoreHelper = graph.addFunction(0x2000, 4, rex::codegen::FunctionAuthority::HELPER, true);
  REQUIRE(restoreHelper != nullptr);
  restoreHelper->setName("__restgprlr_22");

  auto* node = graph.addFunction(0x1000, static_cast<uint32_t>(kFrameSetupAndReturn.size()),
                                 rex::codegen::FunctionAuthority::PDATA, true);
  REQUIRE(node != nullptr);
  node->discover({rex::codegen::Block{.base = 0x1000, .size = 12}}, {}, {});

  rex::codegen::SehExceptionInfo seh;
  seh.frameSize = 320;
  seh.restoreHelper = 0x2000;
  seh.scopes.push_back(rex::codegen::SehScope{
      .tryStart = 0x1000,
      .tryEnd = 0x100C,
      .handler = 0x3000,
      .filter = 0,
  });
  rex::codegen::ExceptionInfo exceptionInfo;
  exceptionInfo.data = std::move(seh);
  graph.setFunctionExceptionInfo(0x1000, std::move(exceptionInfo));
  node->seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node->emitCpp(ctx);
  CHECK(cpp.find("uint32_t seh_establisher_frame = ctx.r1.u32;") != std::string::npos);
  CHECK(cpp.find("ctx.r12.u64 = seh_establisher_frame;  // Establisher frame pointer") !=
        std::string::npos);
  CHECK(cpp.find("ctx.r1.u64 = seh_establisher_frame;  // Restore caller stack pointer") !=
        std::string::npos);
  CHECK(cpp.find("ctx.r12.s64 = ctx.r31.s64 + 320;  // Establisher frame pointer") ==
        std::string::npos);
  RequireTokenOrder(cpp, "ctx.r12.u64 = seh_establisher_frame;  // Establisher frame pointer",
                    "sub_00003000(ctx, base);  // __finally handler");
  RequireTokenOrder(cpp, "ctx.r1.u64 = seh_establisher_frame;  // Restore caller stack pointer",
                    "__restgprlr_22(ctx, base);  // Restore caller registers");
}

TEST_CASE("FunctionNode SEH catch dispatches accepted except handlers",
          "[codegen][FunctionNode][SEH]") {
  constexpr std::array<uint8_t, 16> kTryAndHandler = {
      0x4E, 0x80, 0x00, 0x20,  // blr
      0x60, 0x00, 0x00, 0x00,  // nop
      0x38, 0x60, 0x00, 0x2A,  // li r3,42
      0x4E, 0x80, 0x00, 0x20,  // blr
  };

  auto binary = MakeBinaryView(0x1000, kTryAndHandler);
  rex::codegen::RecompilerConfig config;
  config.generateExceptionHandlers = true;

  rex::codegen::FunctionGraph graph;
  auto* filter = graph.addFunction(0x2000, 4, rex::codegen::FunctionAuthority::HELPER, true);
  REQUIRE(filter != nullptr);
  filter->setName("seh_filter_2000");

  auto* node = graph.addFunction(0x1000, static_cast<uint32_t>(kTryAndHandler.size()),
                                 rex::codegen::FunctionAuthority::PDATA, true);
  REQUIRE(node != nullptr);
  node->discover({rex::codegen::Block{.base = 0x1000, .size = 16}}, {}, {0x1008});

  rex::codegen::SehExceptionInfo seh;
  seh.scopes.push_back(rex::codegen::SehScope{
      .tryStart = 0x1000,
      .tryEnd = 0x1004,
      .handler = 0x1008,
      .filter = 0x2000,
  });
  rex::codegen::ExceptionInfo exceptionInfo;
  exceptionInfo.data = std::move(seh);
  graph.setFunctionExceptionInfo(0x1000, std::move(exceptionInfo));
  node->seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node->emitCpp(ctx);
  CHECK(cpp.find("uint32_t seh_dispatch_target = 0;") != std::string::npos);
  CHECK(cpp.find("if (seh_dispatch_target == 0x00001008) goto loc_1008;") != std::string::npos);
  CHECK(cpp.find("const auto& seh_state = ::rex::platform::seh_thread_state();") !=
        std::string::npos);
  CHECK(cpp.find("!seh_state.raised_by_runtime") != std::string::npos);
  CHECK(cpp.find("seh_state.code == 0xC0000005u") != std::string::npos);
  CHECK(cpp.find("::rex::platform::seh_record_guest_memory_fault("
                 "ctx.last_guest_memory_instruction, ctx.last_guest_memory_operation, "
                 "ctx.last_guest_memory_effective_address);") !=
        std::string::npos);
  CHECK(cpp.find("REXLOG_DEBUG(\"SEH exception caught in sub_00001000: code=0x{:08X}") !=
        std::string::npos);
  CHECK(cpp.find("fault=0x{:X}") != std::string::npos);
  CHECK(cpp.find("last_mem=0x{:08X} last_mem_op={} last_mem_ea=0x{:08X}") != std::string::npos);
  CHECK(cpp.find("fault_mem=0x{:08X} fault_mem_op={} fault_mem_ea=0x{:08X}") != std::string::npos);
  CHECK(cpp.find("seh_state.guest_fault_instruction") != std::string::npos);
  CHECK(cpp.find("seh_state.guest_fault_operation != nullptr") != std::string::npos);
  CHECK(cpp.find("seh_state.guest_fault_effective_address") != std::string::npos);
  CHECK(cpp.find("REXLOG_DEBUG(\"SEH guest regs in sub_00001000: r7=0x{:08X}") !=
        std::string::npos);
  CHECK(cpp.find("const uint32_t seh_saved_r1 = ctx.r1.u32;") != std::string::npos);
  CHECK(cpp.find("ctx.r1.u32 = (ctx.r1.u32 - 0x700) & ~0xFu;") != std::string::npos);
  CHECK(cpp.find("const uint32_t seh_exception_record = ctx.r1.u32 + 0x20;") != std::string::npos);
  CHECK(cpp.find("const uint32_t seh_exception_pointers = ctx.r1.u32 + 0x70;") !=
        std::string::npos);
  CHECK(cpp.find("const uint32_t seh_context_record = ctx.r1.u32 + 0x100;") != std::string::npos);
  CHECK(cpp.find("REX_STORE_U32(seh_exception_record + 0x00, seh_state.code);") !=
        std::string::npos);
  CHECK(cpp.find("REX_STORE_U32(seh_exception_record + 0x14, "
                 "static_cast<uint32_t>(seh_state.info[0]));") != std::string::npos);
  CHECK(cpp.find("REX_STORE_U32(seh_exception_record + 0x18, "
                 "static_cast<uint32_t>(seh_state.info[1]));") != std::string::npos);
  CHECK(cpp.find("REX_STORE_U32(seh_exception_pointers + 0x00, seh_exception_record);") !=
        std::string::npos);
  CHECK(cpp.find("REX_STORE_U32(seh_exception_pointers + 0x04, seh_context_record);") !=
        std::string::npos);
  CHECK(cpp.find("REX_STORE_U64(seh_context_record + 144, ctx.r1.u64);") != std::string::npos);
  CHECK(cpp.find("REX_STORE_U32(seh_context_record + 308, static_cast<uint32_t>(ctx.lr));") !=
        std::string::npos);
  CHECK(cpp.find("REX_STORE_U32(seh_context_record + 8, static_cast<uint32_t>(ctx.lr));") !=
        std::string::npos);
  CHECK(cpp.find("REX_STORE_U64(seh_context_record + 32, ctx.r1.u64);") != std::string::npos);
  CHECK(cpp.find("ctx.r3.u64 = seh_exception_pointers;  // __except exception pointers") !=
        std::string::npos);
  CHECK(cpp.find("seh_filter_2000(ctx, base);  // __except filter") != std::string::npos);
  CHECK(cpp.find("ctx.r1.u32 = seh_saved_r1;") != std::string::npos);
  CHECK(cpp.find("const int32_t seh_filter_result = ctx.r3.s32;") != std::string::npos);
  CHECK(cpp.find("REXLOG_DEBUG(\"SEH filter result in sub_00001000: "
                 "filter=0x00002000 handler=0x00001008") != std::string::npos);
  CHECK(cpp.find("REXLOG_WARN(\"SEH") == std::string::npos);
  CHECK(cpp.find("if (seh_filter_result > 0)") != std::string::npos);
  CHECK(cpp.find("seh_dispatch_target = 0x00001008;") != std::string::npos);
  CHECK(cpp.find("continue;") != std::string::npos);
  CHECK(cpp.find("SEH_RETHROW;") != std::string::npos);
  RequireTokenOrder(cpp, "ctx.r3.u64 = seh_exception_pointers;  // __except exception pointers",
                    "seh_filter_2000(ctx, base);  // __except filter");
  RequireTokenOrder(cpp, "REX_STORE_U32(seh_exception_pointers + 0x04, seh_context_record);",
                    "ctx.r3.u64 = seh_exception_pointers;  // __except exception pointers");
  RequireTokenOrder(cpp, "REX_STORE_U64(seh_context_record + 32, ctx.r1.u64);",
                    "ctx.r3.u64 = seh_exception_pointers;  // __except exception pointers");
  RequireTokenOrder(cpp, "seh_filter_2000(ctx, base);  // __except filter",
                    "ctx.r1.u32 = seh_saved_r1;");
  RequireTokenOrder(cpp, "ctx.r1.u32 = seh_saved_r1;",
                    "const int32_t seh_filter_result = ctx.r3.s32;");
  RequireTokenOrder(cpp, "const int32_t seh_filter_result = ctx.r3.s32;",
                    "seh_dispatch_target = 0x00001008;");
  RequireTokenOrder(cpp, "if (seh_dispatch_target == 0x00001008) goto loc_1008;", "loc_1008:");
}

TEST_CASE("FunctionNode records last guest memory instruction for SEH diagnostics",
          "[codegen][FunctionNode][SEH]") {
  constexpr std::array<uint8_t, 8> kLoadAndReturn = {
      0x80, 0x64, 0x00, 0x00,  // lwz r3,0(r4)
      0x4E, 0x80, 0x00, 0x20,  // blr
  };

  auto binary = MakeBinaryView(0x1000, kLoadAndReturn);
  rex::codegen::RecompilerConfig config;
  config.generateExceptionHandlers = true;

  rex::codegen::FunctionGraph graph;
  auto* node = graph.addFunction(0x1000, static_cast<uint32_t>(kLoadAndReturn.size()),
                                 rex::codegen::FunctionAuthority::PDATA, true);
  REQUIRE(node != nullptr);
  node->discover({rex::codegen::Block{.base = 0x1000, .size = 8}}, {}, {});

  rex::codegen::SehExceptionInfo seh;
  seh.scopes.push_back(rex::codegen::SehScope{
      .tryStart = 0x1000,
      .tryEnd = 0x1008,
      .handler = 0x3000,
      .filter = 0,
  });
  rex::codegen::ExceptionInfo exceptionInfo;
  exceptionInfo.data = std::move(seh);
  graph.setFunctionExceptionInfo(0x1000, std::move(exceptionInfo));
  node->seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node->emitCpp(ctx);
  CHECK(cpp.find("ctx.last_guest_memory_instruction = 0x00001000;") != std::string::npos);
  CHECK(cpp.find("ctx.last_guest_memory_operation = \"lwz\";") != std::string::npos);
  CHECK(cpp.find("ctx.last_guest_memory_effective_address = ctx.r4.u32 + 0;") !=
        std::string::npos);
  RequireTokenOrder(cpp, "ctx.last_guest_memory_instruction = 0x00001000;",
                    "ctx.r3.u64 = REX_LOAD_U32(ctx.r4.u32 + 0);");
  RequireTokenOrder(cpp, "ctx.last_guest_memory_operation = \"lwz\";",
                    "ctx.last_guest_memory_effective_address = ctx.r4.u32 + 0;");
  RequireTokenOrder(cpp, "ctx.last_guest_memory_effective_address = ctx.r4.u32 + 0;",
                    "ctx.r3.u64 = REX_LOAD_U32(ctx.r4.u32 + 0);");
}
TEST_CASE("FunctionNode SEH dispatch calls discovered out-of-block handler entries",
          "[codegen][FunctionNode][SEH]") {
  constexpr std::array<uint8_t, 4> kTryOnly = {
      0x4E, 0x80, 0x00, 0x20,  // blr
  };

  auto binary = MakeBinaryView(0x1000, kTryOnly);
  rex::codegen::RecompilerConfig config;
  config.generateExceptionHandlers = true;

  rex::codegen::FunctionGraph graph;
  auto* filter = graph.addFunction(0x2000, 4, rex::codegen::FunctionAuthority::HELPER, true);
  REQUIRE(filter != nullptr);
  filter->setName("seh_filter_2000");
  auto* handler = graph.addFunction(0x1040, 4, rex::codegen::FunctionAuthority::DISCOVERED, true);
  REQUIRE(handler != nullptr);
  handler->setName("seh_handler_1040");

  auto* node = graph.addFunction(0x1000, 0x80, rex::codegen::FunctionAuthority::PDATA, true);
  REQUIRE(node != nullptr);
  node->discover({rex::codegen::Block{.base = 0x1000, .size = 4}}, {}, {});

  rex::codegen::SehExceptionInfo seh;
  seh.scopes.push_back(rex::codegen::SehScope{
      .tryStart = 0x1000,
      .tryEnd = 0x1004,
      .handler = 0x1040,
      .filter = 0x2000,
  });
  rex::codegen::ExceptionInfo exceptionInfo;
  exceptionInfo.data = std::move(seh);
  graph.setFunctionExceptionInfo(0x1000, std::move(exceptionInfo));
  node->seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node->emitCpp(ctx);
  CHECK(cpp.find("if (seh_dispatch_target == 0x00001040) {") != std::string::npos);
  CHECK(cpp.find("seh_handler_1040(ctx, base);") != std::string::npos);
  CHECK(cpp.find("if (seh_dispatch_target == 0x00001040) goto loc_1040;") == std::string::npos);
  CHECK(cpp.find("REX_FATAL(\"Branch target 0x00001040 in sub_00001000 has no emitted block\");") ==
        std::string::npos);
  RequireTokenOrder(cpp, "if (seh_dispatch_target == 0x00001040) {",
                    "seh_handler_1040(ctx, base);");
  RequireTokenOrder(cpp, "seh_handler_1040(ctx, base);", "\t\t\treturn;");
}

TEST_CASE("FunctionNode SEH dispatch prefers separate handler entries over overlapping blocks",
          "[codegen][FunctionNode][SEH]") {
  std::array<uint8_t, 0x44> bytes{};
  for (size_t offset = 0; offset < bytes.size(); offset += 4) {
    bytes[offset + 0] = 0x60;
    bytes[offset + 1] = 0x00;
    bytes[offset + 2] = 0x00;
    bytes[offset + 3] = 0x00;  // nop
  }
  bytes[0x40] = 0x4E;
  bytes[0x41] = 0x80;
  bytes[0x42] = 0x00;
  bytes[0x43] = 0x20;  // blr

  auto binary = MakeBinaryView(0x1000, bytes);
  rex::codegen::RecompilerConfig config;
  config.generateExceptionHandlers = true;

  rex::codegen::FunctionGraph graph;
  auto* filter = graph.addFunction(0x2000, 4, rex::codegen::FunctionAuthority::HELPER, true);
  REQUIRE(filter != nullptr);
  filter->setName("seh_filter_2000");
  auto* handler = graph.addFunction(0x1040, 4, rex::codegen::FunctionAuthority::DISCOVERED, true);
  REQUIRE(handler != nullptr);
  handler->setName("seh_handler_1040");

  auto* node = graph.addFunction(0x1000, 0x80, rex::codegen::FunctionAuthority::PDATA, true);
  REQUIRE(node != nullptr);
  node->discover({rex::codegen::Block{.base = 0x1000, .size = static_cast<uint32_t>(bytes.size())}},
                 {}, {});

  rex::codegen::SehExceptionInfo seh;
  seh.scopes.push_back(rex::codegen::SehScope{
      .tryStart = 0x1000,
      .tryEnd = 0x1004,
      .handler = 0x1040,
      .filter = 0x2000,
  });
  rex::codegen::ExceptionInfo exceptionInfo;
  exceptionInfo.data = std::move(seh);
  graph.setFunctionExceptionInfo(0x1000, std::move(exceptionInfo));
  node->seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node->emitCpp(ctx);
  CHECK(cpp.find("if (seh_dispatch_target == 0x00001040) {") != std::string::npos);
  CHECK(cpp.find("seh_handler_1040(ctx, base);") != std::string::npos);
  CHECK(cpp.find("if (seh_dispatch_target == 0x00001040) goto loc_1040;") == std::string::npos);
  RequireTokenOrder(cpp, "if (seh_dispatch_target == 0x00001040) {",
                    "seh_handler_1040(ctx, base);");
  RequireTokenOrder(cpp, "seh_handler_1040(ctx, base);", "\t\t\treturn;");
}

TEST_CASE("FunctionNode SEH dispatch emits fatal stubs for missing in-range handler blocks",
          "[codegen][FunctionNode][SEH]") {
  constexpr std::array<uint8_t, 4> kTryOnly = {
      0x4E, 0x80, 0x00, 0x20,  // blr
  };

  auto binary = MakeBinaryView(0x1000, kTryOnly);
  rex::codegen::RecompilerConfig config;
  config.generateExceptionHandlers = true;

  rex::codegen::FunctionGraph graph;
  auto* filter = graph.addFunction(0x2000, 4, rex::codegen::FunctionAuthority::HELPER, true);
  REQUIRE(filter != nullptr);
  filter->setName("seh_filter_2000");

  auto* node = graph.addFunction(0x1000, 0x80, rex::codegen::FunctionAuthority::PDATA, true);
  REQUIRE(node != nullptr);
  node->discover({rex::codegen::Block{.base = 0x1000, .size = 4}}, {}, {});

  rex::codegen::SehExceptionInfo seh;
  seh.scopes.push_back(rex::codegen::SehScope{
      .tryStart = 0x1000,
      .tryEnd = 0x1004,
      .handler = 0x1040,
      .filter = 0x2000,
  });
  rex::codegen::ExceptionInfo exceptionInfo;
  exceptionInfo.data = std::move(seh);
  graph.setFunctionExceptionInfo(0x1000, std::move(exceptionInfo));
  node->seal();

  rex::codegen::EmitContext ctx{
      .binary = binary,
      .config = config,
      .graph = graph,
      .entryPoint = 0,
      .resolver = nullptr,
  };

  const std::string cpp = node->emitCpp(ctx);
  CHECK(cpp.find("if (seh_dispatch_target == 0x00001040) goto loc_1040;") != std::string::npos);
  CHECK(cpp.find("loc_1040:") != std::string::npos);
  CHECK(cpp.find("REX_FATAL(\"Branch target 0x00001040 in sub_00001000 has no emitted block\");") !=
        std::string::npos);
  RequireTokenOrder(cpp, "if (seh_dispatch_target == 0x00001040) goto loc_1040;", "loc_1040:");
}
