#include <chrono>

#include <catch2/catch_test_macros.hpp>
#include <rex/codegen/function_graph.h>

TEST_CASE("FunctionGraph registers bulk PDATA entries without unresolved-jump scans",
          "[codegen][FunctionGraph]") {
  rex::codegen::FunctionGraph graph;

  constexpr uint32_t kBase = 0x82000000;
  constexpr uint32_t kFunctionCount = 12000;
  constexpr uint32_t kFunctionStride = 0x10;

  const auto start = std::chrono::steady_clock::now();

  for (uint32_t i = 0; i < kFunctionCount; ++i) {
    graph.addFunction(kBase + i * kFunctionStride, kFunctionStride,
                      rex::codegen::FunctionAuthority::PDATA, true);
  }

  const auto elapsed = std::chrono::steady_clock::now() - start;
  const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

  CHECK(graph.functionCount() == kFunctionCount);
  CHECK(elapsedMs < 1500);
}

TEST_CASE("FunctionGraph resolves recorded unresolved jumps when the target is added",
          "[codegen][FunctionGraph]") {
  rex::codegen::FunctionGraph graph;

  auto* source =
      graph.addFunction(0x82001000, 4, rex::codegen::FunctionAuthority::DISCOVERED, true);
  graph.addUnresolvedJumpToFunction(0x82001000, 0x82001000, 0x82002000, false, false);

  REQUIRE(source->unresolvedJumps().size() == 1);

  auto* target =
      graph.addFunction(0x82002000, 4, rex::codegen::FunctionAuthority::DISCOVERED, true);

  CHECK(source->unresolvedJumps().empty());
  REQUIRE(source->tailCalls().size() == 1);
  CHECK(source->tailCalls()[0].site == 0x82001000);
  CHECK(source->tailCalls()[0].target.asFunction() == target);
}

TEST_CASE("FunctionGraph preserves resolved call target pointers when authority is upgraded",
          "[codegen][FunctionGraph]") {
  rex::codegen::FunctionGraph graph;

  auto* source =
      graph.addFunction(0x82001000, 4, rex::codegen::FunctionAuthority::DISCOVERED, true);
  graph.addUnresolvedJumpToFunction(0x82001000, 0x82001000, 0x82002000, false, false);

  auto* speculative =
      graph.addFunction(0x82002000, 4, rex::codegen::FunctionAuthority::GAP_FILL, false);

  REQUIRE(source->tailCalls().size() == 1);
  REQUIRE(source->tailCalls()[0].target.asFunction() == speculative);

  auto* upgraded =
      graph.addFunction(0x82002000, 0x40, rex::codegen::FunctionAuthority::DISCOVERED, true);

  CHECK(upgraded == speculative);
  CHECK(graph.getFunction(0x82002000) == speculative);
  CHECK(source->tailCalls()[0].target.asFunction() == graph.getFunction(0x82002000));
  CHECK(upgraded->authority() == rex::codegen::FunctionAuthority::DISCOVERED);
  CHECK(upgraded->size() == 0x40);
}

TEST_CASE("FunctionGraph keeps referenced speculative targets alive during cleanup",
          "[codegen][FunctionGraph]") {
  rex::codegen::FunctionGraph graph;

  auto* source =
      graph.addFunction(0x82001000, 4, rex::codegen::FunctionAuthority::DISCOVERED, true);
  auto* target =
      graph.addFunction(0x82002000, 4, rex::codegen::FunctionAuthority::GAP_FILL, false);

  graph.addUnresolvedJumpToFunction(0x82001000, 0x82001000, 0x82002000, false, false);

  REQUIRE(source->tailCalls().size() == 1);
  REQUIRE(source->tailCalls()[0].target.asFunction() == target);

  CHECK_FALSE(graph.removeFunction(0x82002000));
  CHECK(graph.getFunction(0x82002000) == target);
  CHECK(source->tailCalls()[0].target.asFunction() == graph.getFunction(0x82002000));
}

TEST_CASE("FunctionGraph containing lookup handles overlapping ranges",
          "[codegen][FunctionGraph]") {
  rex::codegen::FunctionGraph graph;

  auto* wide = graph.addFunction(0x82001000, 0x100, rex::codegen::FunctionAuthority::PDATA, true);
  REQUIRE(wide != nullptr);

  auto* narrow =
      graph.addFunction(0x82001050, 4, rex::codegen::FunctionAuthority::DISCOVERED, true);
  REQUIRE(narrow != nullptr);

  CHECK(graph.getFunctionContaining(0x82001050) == narrow);
  CHECK(graph.getFunctionContaining(0x82001058) == wide);
}
