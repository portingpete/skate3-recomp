#pragma once

#include <catch2/catch_test_macros.hpp>

#include <rex/logging.h>
#include <rex/system/xmemory.h>

namespace rex::test {

inline rex::memory::Memory& GetTestMemory() {
  static rex::memory::Memory memory;
  static bool initialized = false;
  if (!initialized) {
    rex::InitLogging();
    REQUIRE(memory.Initialize());
    initialized = true;
  }
  return memory;
}

}  // namespace rex::test
