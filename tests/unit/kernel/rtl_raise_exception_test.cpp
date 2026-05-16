#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include <rex/platform/exceptions.h>
#include <rex/platform/seh.h>
#include <rex/ppc/context.h>
#include <rex/system/xexception.h>

extern "C" REX_FUNC(__imp__RtlRaiseException);

namespace {

struct RaisedSehCapture {
  bool caught = false;
  uint32_t code = 0;
  uintptr_t info0 = 0;
  uintptr_t info1 = 0;
};

RaisedSehCapture CaptureRtlRaiseException() {
  alignas(32) uint8_t storage[0x200] = {};
  constexpr uint32_t kRecordAddress = 0x40;

  auto* record = reinterpret_cast<rex::system::X_EXCEPTION_RECORD*>(storage + kRecordAddress);
  record->code = 0xE1234567u;
  record->exception_flags = 0u;
  record->exception_record = 0u;
  record->exception_address = 0x820BA16Cu;
  record->number_parameters = 2u;
  record->exception_information[0] = 0x11112222u;
  record->exception_information[1] = 0x33334444u;

  PPCContext ctx{};
  ctx.r3.u32 = kRecordAddress;

  RaisedSehCapture result{};
  SEH_TRY {
    __imp__RtlRaiseException(ctx, storage);
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

}  // namespace

TEST_CASE("RtlRaiseException routes guest exceptions into generated SEH state",
          "[kernel][rtl]") {
  const auto capture = CaptureRtlRaiseException();
  CHECK(capture.caught);
  CHECK(capture.code == 0xE1234567u);
  CHECK(capture.info0 == 0x11112222u);
  CHECK(capture.info1 == 0x33334444u);
}
