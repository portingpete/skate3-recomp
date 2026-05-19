/**
 * @file        tests/unit/system/xthread_test.cpp
 * @brief       Unit tests for guest thread diagnostics
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <catch2/catch_test_macros.hpp>
#include <rex/system/xthread.h>

using rex::system::DescribeGuestThreadStart;

TEST_CASE("XThread diagnostics describe raw guest thread entries", "[system][xthread]") {
  CHECK(DescribeGuestThreadStart(0x820B7090, 0x820B7090, 0x40001880, 0) ==
        "entry=0x820B7090 (sub_820B7090), context=0x40001880");
}

TEST_CASE("XThread diagnostics include guest start behind Xapi startup trampolines",
          "[system][xthread]") {
  CHECK(DescribeGuestThreadStart(0x82001000, 0x820B7090, 0x40001880, 0x82001000) ==
        "entry=0x82001000 (sub_82001000), start=0x820B7090 (sub_820B7090), "
        "context=0x40001880");
}
