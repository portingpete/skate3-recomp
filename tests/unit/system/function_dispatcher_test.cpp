/**
 * @file        tests/unit/system/function_dispatcher_test.cpp
 * @brief       Unit tests for caller-aware thunk allocation and unregister cleanup
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <catch2/catch_test_macros.hpp>

#include "../test_memory.h"

#include <rex/ppc/context.h>
#include <rex/system/export_resolver.h>
#include <rex/system/function_dispatcher.h>

namespace {

using rex::test::GetTestMemory;

void DummyFn(PPCContext&, uint8_t*) {}

}  // namespace

TEST_CASE("FunctionDispatcher: caller_address routes thunk to caller's module pool",
          "[runtime][dispatcher]") {
  auto& memory = GetTestMemory();
  rex::runtime::ExportResolver resolver;
  rex::runtime::FunctionDispatcher dispatcher(&memory, &resolver);

  constexpr uint32_t kModA = 0x82000000u;
  constexpr uint32_t kCodeSize = 0x10000u;
  constexpr uint32_t kImageSize = 0x100000u;

  REQUIRE(dispatcher.InitializeFunctionTable(kModA, kCodeSize, kModA, kImageSize));

  constexpr uint32_t kModB = 0x83000000u;
  REQUIRE(dispatcher.InitializeFunctionTable(kModB, kCodeSize, kModB, kImageSize));

  uint32_t thunk_a = dispatcher.AllocateThunk(&DummyFn, kModA + 0x100);
  uint32_t thunk_b = dispatcher.AllocateThunk(&DummyFn, kModB + 0x100);

  CHECK(thunk_a >= kModA + kCodeSize);
  CHECK(thunk_a < kModA + kCodeSize + rex::runtime::FunctionDispatcher::kThunkReserveSize);
  CHECK(thunk_b >= kModB + kCodeSize);
  CHECK(thunk_b < kModB + kCodeSize + rex::runtime::FunctionDispatcher::kThunkReserveSize);

  CHECK(dispatcher.GetFunction(thunk_a) == &DummyFn);
  CHECK(dispatcher.GetFunction(thunk_b) == &DummyFn);
}

TEST_CASE("FunctionDispatcher: AllocateThunk(0) uses the entrypoint pool only when explicit",
          "[runtime][dispatcher]") {
  // caller_address=0 is reserved for host-initiated allocations that have no
  // guest caller (the entrypoint wiring its own __imp__* exports during
  // setup). It must land in the entrypoint module's pool.
  auto& memory = GetTestMemory();
  rex::runtime::ExportResolver resolver;
  rex::runtime::FunctionDispatcher dispatcher(&memory, &resolver);

  constexpr uint32_t kModA = 0x84000000u;
  constexpr uint32_t kCodeSize = 0x10000u;
  constexpr uint32_t kImageSize = 0x100000u;

  REQUIRE(dispatcher.InitializeFunctionTable(kModA, kCodeSize, kModA, kImageSize,
                                             /*is_entrypoint=*/true));

  uint32_t thunk = dispatcher.AllocateThunk(&DummyFn, 0);
  CHECK(thunk >= kModA + kCodeSize);
  CHECK(thunk < kModA + kCodeSize + rex::runtime::FunctionDispatcher::kThunkReserveSize);
}

TEST_CASE("FunctionDispatcher: AllocateThunk(0) rejects when no entrypoint registered",
          "[runtime][dispatcher]") {
  auto& memory = GetTestMemory();
  rex::runtime::ExportResolver resolver;
  rex::runtime::FunctionDispatcher dispatcher(&memory, &resolver);

  constexpr uint32_t kModA = 0x88000000u;
  constexpr uint32_t kCodeSize = 0x10000u;
  constexpr uint32_t kImageSize = 0x100000u;

  REQUIRE(dispatcher.InitializeFunctionTable(kModA, kCodeSize, kModA, kImageSize));

  CHECK(dispatcher.AllocateThunk(&DummyFn, 0) == 0);
}

TEST_CASE("FunctionDispatcher: AllocateThunk rejects caller_address outside any module",
          "[runtime][dispatcher]") {
  auto& memory = GetTestMemory();
  rex::runtime::ExportResolver resolver;
  rex::runtime::FunctionDispatcher dispatcher(&memory, &resolver);

  constexpr uint32_t kModA = 0x86000000u;
  constexpr uint32_t kCodeSize = 0x10000u;
  constexpr uint32_t kImageSize = 0x100000u;

  REQUIRE(dispatcher.InitializeFunctionTable(kModA, kCodeSize, kModA, kImageSize));

  // A non-zero caller_address that doesn't fall inside any module is a bug
  // in the caller: the right answer is to refuse, not to silently route the
  // thunk to the entrypoint pool.
  uint32_t thunk = dispatcher.AllocateThunk(&DummyFn, 0xDEADBEEFu);
  CHECK(thunk == 0);
}

TEST_CASE("FunctionDispatcher: indirect dispatch generation tracks mapping changes",
          "[runtime][dispatcher]") {
  auto& memory = GetTestMemory();
  rex::runtime::ExportResolver resolver;
  rex::runtime::FunctionDispatcher dispatcher(&memory, &resolver);

  constexpr uint32_t kModA = 0x87000000u;
  constexpr uint32_t kModB = 0x87200000u;
  constexpr uint32_t kCodeSize = 0x10000u;
  constexpr uint32_t kImageSize = 0x100000u;

  const uint64_t before_init = rex::runtime::IndirectDispatchGeneration();
  REQUIRE(dispatcher.InitializeFunctionTable(kModA, kCodeSize, kModA, kImageSize));
  CHECK(rex::runtime::IndirectDispatchGeneration() == before_init);

  CHECK_FALSE(dispatcher.SetFunction(0xDEADBEEFu, &DummyFn));
  CHECK(rex::runtime::IndirectDispatchGeneration() == before_init);

  REQUIRE(dispatcher.SetFunction(kModA + 0x10, &DummyFn));
  const uint64_t after_set = rex::runtime::IndirectDispatchGeneration();
  CHECK(after_set > before_init);

  CHECK(dispatcher.AllocateThunk(&DummyFn, 0xDEADBEEFu) == 0);
  CHECK(rex::runtime::IndirectDispatchGeneration() == after_set);

  const uint32_t thunk = dispatcher.AllocateThunk(&DummyFn, kModA + 0x100);
  REQUIRE(thunk != 0);
  const uint64_t after_thunk = rex::runtime::IndirectDispatchGeneration();
  CHECK(after_thunk > after_set);

  REQUIRE(dispatcher.InitializeFunctionTable(kModB, kCodeSize, kModB, kImageSize));
  CHECK(rex::runtime::IndirectDispatchGeneration() == after_thunk);

  dispatcher.RegisterModule("modGeneration", kModB, [](rex::runtime::IModuleRegistrar* registrar) {
    registrar->SetFunction(0x87200020u, &DummyFn);
  });
  const uint64_t after_register = rex::runtime::IndirectDispatchGeneration();
  CHECK(after_register > after_thunk);

  CHECK_FALSE(dispatcher.UnregisterModule("missingGeneration").has_value());
  CHECK(rex::runtime::IndirectDispatchGeneration() == after_register);

  auto cleared = dispatcher.UnregisterModule("modGeneration");
  REQUIRE(cleared.has_value());
  CHECK(rex::runtime::IndirectDispatchGeneration() > after_register);
}

namespace {
constexpr uint32_t kRegisterModBase = 0x85000000u;
void RegisterOne(rex::runtime::IModuleRegistrar* registrar) {
  registrar->SetFunction(kRegisterModBase + 0x10, &DummyFn);
}
}  // namespace

TEST_CASE("FunctionDispatcher: UnregisterModule clears pool and slots", "[runtime][dispatcher]") {
  auto& memory = GetTestMemory();
  rex::runtime::ExportResolver resolver;
  rex::runtime::FunctionDispatcher dispatcher(&memory, &resolver);

  constexpr uint32_t kCodeSize = 0x10000u;
  constexpr uint32_t kImageSize = 0x100000u;

  REQUIRE(dispatcher.InitializeFunctionTable(kRegisterModBase, kCodeSize, kRegisterModBase,
                                             kImageSize));

  dispatcher.RegisterModule("modA", kRegisterModBase, &RegisterOne);

  uint32_t thunk = dispatcher.AllocateThunk(&DummyFn, kRegisterModBase + 0x100);
  REQUIRE(thunk != 0);
  REQUIRE(dispatcher.GetFunction(kRegisterModBase + 0x10) == &DummyFn);
  REQUIRE(dispatcher.GetFunction(thunk) == &DummyFn);

  auto cleared = dispatcher.UnregisterModule("modA");
  REQUIRE(cleared.has_value());
  CHECK(cleared->first == kRegisterModBase + kCodeSize);
  CHECK(cleared->second == thunk + 4);
  CHECK(dispatcher.GetFunction(kRegisterModBase + 0x10) == nullptr);
  CHECK(dispatcher.GetFunction(thunk) == nullptr);

  // Re-init must succeed: UnregisterModule destroys the per-module table so the
  // same code range can be reloaded without tripping the overlap check.
  REQUIRE(dispatcher.InitializeFunctionTable(kRegisterModBase, kCodeSize, kRegisterModBase,
                                             kImageSize));
  uint32_t thunk_after = dispatcher.AllocateThunk(&DummyFn, kRegisterModBase + 0x100);
  CHECK(thunk_after == thunk);
}

TEST_CASE("FunctionDispatcher: UnregisterModule on unknown id returns nullopt",
          "[runtime][dispatcher]") {
  auto& memory = GetTestMemory();
  rex::runtime::ExportResolver resolver;
  rex::runtime::FunctionDispatcher dispatcher(&memory, &resolver);
  CHECK_FALSE(dispatcher.UnregisterModule("nope").has_value());
}
