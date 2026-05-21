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

#include <cstddef>

#include <catch2/catch_test_macros.hpp>
#include <rex/system/xthread.h>

using rex::system::DescribeGuestThreadStart;
using rex::system::DescribeUnhandledGuestThreadException;

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

TEST_CASE("XThread diagnostics preserve high fault address bits", "[system][xthread]") {
  CHECK(DescribeUnhandledGuestThreadException(6, 0x820B7090, 0x820B7090, 0, 0, 0xC0000005,
                                             0x100000000ull) ==
        "thid=6, entry=0x820B7090 (sub_820B7090), context=0x00000000, code=0xC0000005, "
        "fault=0x0000000100000000");
}

TEST_CASE("XThread diagnostics include preserved guest memory fault breadcrumbs",
          "[system][xthread]") {
  CHECK(DescribeUnhandledGuestThreadException(6, 0x820B7090, 0x820B7090, 0, 0, 0xC0000005,
                                             0x100000000ull, 0x820B861C, "lwz") ==
        "thid=6, entry=0x820B7090 (sub_820B7090), context=0x00000000, code=0xC0000005, "
        "fault=0x0000000100000000, fault_mem=0x820B861C fault_mem_op=lwz");
}

TEST_CASE("XThread layout names the kernel time field at the observed KTHREAD offset",
          "[system][xthread][layout]") {
  CHECK(offsetof(rex::system::X_KTHREAD, kernel_time) == 0x58);
  CHECK(offsetof(rex::system::X_KTHREAD, stack_base) == 0x5C);
  CHECK(offsetof(rex::system::X_KTHREAD, stack_base) -
            offsetof(rex::system::X_KTHREAD, kernel_time) ==
        sizeof(uint32_t));
}
