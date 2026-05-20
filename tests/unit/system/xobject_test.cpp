#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

#include <rex/chrono/clock.h>
#include <rex/system/xobject.h>

namespace {

class TimeoutAccess final : public rex::system::XObject {
 public:
  TimeoutAccess() : XObject(Type::Undefined) {}

  using XObject::TimeoutTicksToMs;
};

}  // namespace

TEST_CASE("kernel timeout conversion floors relative 100ns ticks to host milliseconds",
          "[system][xobject]") {
  CHECK(TimeoutAccess::TimeoutTicksToMs(0) == 0);
  CHECK(TimeoutAccess::TimeoutTicksToMs(-1) == 0);
  CHECK(TimeoutAccess::TimeoutTicksToMs(-9999) == 0);
  CHECK(TimeoutAccess::TimeoutTicksToMs(-10000) == 1);
  CHECK(TimeoutAccess::TimeoutTicksToMs(-10001) == 1);
}

TEST_CASE("kernel timeout conversion clamps very long relative waits",
          "[system][xobject]") {
  CHECK(TimeoutAccess::TimeoutTicksToMs(std::numeric_limits<int64_t>::min()) ==
        std::numeric_limits<uint32_t>::max());
}

TEST_CASE("kernel timeout conversion treats past absolute waits as expired",
          "[system][xobject]") {
  const uint64_t now = rex::chrono::Clock::QueryGuestSystemTime();

  REQUIRE(now > 0);
  CHECK(TimeoutAccess::TimeoutTicksToMs(static_cast<int64_t>(now - 1)) == 0);
}