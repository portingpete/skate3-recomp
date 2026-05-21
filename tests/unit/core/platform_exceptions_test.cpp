#include <catch2/catch_test_macros.hpp>
#include <rex/platform.h>
#include <rex/platform/exceptions.h>
#include <rex/platform/seh.h>

#include <string_view>

#if REX_PLATFORM_WIN32
#include <windows.h>
#endif

namespace {

int CompileSehMacros() {
  SEH_TRY {
    return 1;
  }
  SEH_CATCH_ALL {
    SEH_RETHROW;
  }
  SEH_END
}

struct RaisedSehCapture {
  bool caught = false;
  uint32_t code = 0;
  uintptr_t info0 = 0;
  uintptr_t info1 = 0;
};

RaisedSehCapture CapturePlatformSehRaise() {
  RaisedSehCapture result{};
  SEH_TRY {
    rex::platform::seh_raise(0xE1234567u, 0x11112222u, 0x33334444u, 2u);
  }
  SEH_CATCH_ALL {
    const auto& seh_state = rex::platform::seh_thread_state();
    result.caught = true;
    result.code = seh_state.code;
    result.info0 = seh_state.info[0];
    result.info1 = seh_state.info[1];
  }
  SEH_END
  return result;
}

#if REX_PLATFORM_WIN32
RaisedSehCapture CaptureThreadBoundarySehRaise() {
  RaisedSehCapture result{};
  __try {
    rex::platform::seh_raise(0xE7654321u, 0xAAAABBBBu, 0xCCCCDDDDu, 2u);
  } __except (rex::platform::seh_thread_boundary_filter(GetExceptionCode(),
                                                        GetExceptionInformation())) {
    const auto& seh_state = rex::platform::seh_thread_state();
    result.caught = true;
    result.code = seh_state.code;
    result.info0 = seh_state.info[0];
    result.info1 = seh_state.info[1];
  }
  return result;
}

RaisedSehCapture CaptureThreadBoundaryNativeRaise() {
  RaisedSehCapture result{};
  __try {
    __try {
      ULONG_PTR info[2] = {0, 0x12345678u};
      RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 2, info);
    } __except (rex::platform::seh_thread_boundary_filter(GetExceptionCode(),
                                                          GetExceptionInformation())) {
      result.caught = true;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    const auto& seh_state = rex::platform::seh_thread_state();
    result.code = seh_state.code;
    result.info0 = seh_state.info[0];
    result.info1 = seh_state.info[1];
  }
  return result;
}
#endif

}  // namespace

TEST_CASE("Platform exceptions expose SEH macros as a self-contained header",
          "[core][platform]") {
  CHECK(CompileSehMacros() == 1);
}

TEST_CASE("Platform SEH raise captures guest exception payloads",
          "[core][platform]") {
  const auto capture = CapturePlatformSehRaise();
  CHECK(capture.caught);
  CHECK(capture.code == 0xE1234567u);
  CHECK(capture.info0 == 0x11112222u);
  CHECK(capture.info1 == 0x33334444u);
}

TEST_CASE("Platform SEH guest memory breadcrumb keeps the first faulting op",
          "[core][platform]") {
  rex::platform::seh_clear_guest_memory_fault();
  auto& seh_state = rex::platform::seh_thread_state();

  rex::platform::seh_record_guest_memory_fault(0x00001000u, "lwz");
  rex::platform::seh_record_guest_memory_fault(0x00002000u, "stw");

  CHECK(seh_state.guest_fault_instruction == 0x00001000u);
  REQUIRE(seh_state.guest_fault_operation != nullptr);
  CHECK(std::string_view(seh_state.guest_fault_operation) == "lwz");

  rex::platform::seh_clear_guest_memory_fault();
  CHECK(seh_state.guest_fault_instruction == 0u);
  CHECK(seh_state.guest_fault_operation == nullptr);
}

#if REX_PLATFORM_WIN32
TEST_CASE("Thread boundary SEH catches runtime-raised guest exceptions",
          "[core][platform]") {
  const auto capture = CaptureThreadBoundarySehRaise();
  CHECK(capture.caught);
  CHECK(capture.code == 0xE7654321u);
  CHECK(capture.info0 == 0xAAAABBBBu);
  CHECK(capture.info1 == 0xCCCCDDDDu);
}

TEST_CASE("Thread boundary SEH leaves native exceptions to outer handlers",
          "[core][platform]") {
  const auto capture = CaptureThreadBoundaryNativeRaise();
  CHECK_FALSE(capture.caught);
  CHECK(capture.code == EXCEPTION_ACCESS_VIOLATION);
  CHECK(capture.info0 == 0u);
  CHECK(capture.info1 == 0x12345678u);
}
#endif
