/**
 * @file        ppc_delay_test.cpp
 * @brief       Unit tests for PPC delay execution hint policy
 *
 * @copyright   Copyright (c) 2026 Tom Clay
 * @license     BSD 3-Clause License
 */

#include <string>

#include <catch2/catch_test_macros.hpp>

#include <rex/cvar.h>
#include <rex/ppc/context.h>

TEST_CASE("PPC delay execution hint can opt into MaybeYield policy", "[ppc][delay]") {
  const std::string old_value = rex::cvar::GetFlagByName("ppc_delay_via_maybeyield");

  REQUIRE(rex::cvar::SetFlagByName("ppc_delay_via_maybeyield", "false"));
  CHECK_FALSE(REXCVAR_GET(ppc_delay_via_maybeyield));

  REQUIRE(rex::cvar::SetFlagByName("ppc_delay_via_maybeyield", "true"));
  CHECK(REXCVAR_GET(ppc_delay_via_maybeyield));

  REQUIRE(rex::cvar::SetFlagByName("ppc_delay_via_maybeyield", old_value));
}
