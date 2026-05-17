/**
 * @file        ppc_memory_compat_test.cpp
 * @brief       Tests for legacy PPC memory helper compatibility
 *
 * @copyright   Copyright (c) 2026 Tom Clay
 * @license     BSD 3-Clause License
 */

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include <rex/memory.h>

TEST_CASE("Legacy PPC no-fault U32 helpers store and load big-endian values",
          "[memory][ppc][compat]") {
  std::array<uint8_t, 64> membase{};

  REQUIRE(rex::ppc::TryStoreU32NoFault(membase.data(), 0x10, 0x11223344));
  CHECK(membase[0x10] == 0x11);
  CHECK(membase[0x11] == 0x22);
  CHECK(membase[0x12] == 0x33);
  CHECK(membase[0x13] == 0x44);

  uint32_t value = 0;
  REQUIRE(rex::ppc::TryLoadU32NoFault(membase.data(), 0x10, &value));
  CHECK(value == 0x11223344);
}
