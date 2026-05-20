#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <rex/types.h>

namespace rex::kernel::xboxkrnl {
u64 KeQueryInterruptTime_entry();

namespace internal {
uint32_t WriteKeTimeStampBundle(uint8_t* bundle);
}  // namespace internal
}  // namespace rex::kernel::xboxkrnl

namespace {

struct TestTimeStampBundle {
  rex::be<uint64_t> interrupt_time;
  rex::be<uint64_t> system_time;
  rex::be<uint32_t> tick_count;
  rex::be<uint32_t> padding;
};

static_assert(sizeof(TestTimeStampBundle) == 24);

}  // namespace

TEST_CASE("KeQueryInterruptTime returns advancing guest interrupt time",
          "[kernel][xboxkrnl][time]") {
  const uint64_t first = rex::kernel::xboxkrnl::KeQueryInterruptTime_entry();
  const uint64_t second = rex::kernel::xboxkrnl::KeQueryInterruptTime_entry();

  CHECK(first > 0);
  CHECK(second >= first);
}

TEST_CASE("KeTimeStampBundle writer fills interrupt, system, and tick fields",
          "[kernel][xboxkrnl][time]") {
  std::array<uint8_t, sizeof(TestTimeStampBundle)> storage{};
  auto* bundle = reinterpret_cast<TestTimeStampBundle*>(storage.data());

  const uint32_t returned_tick_count =
      rex::kernel::xboxkrnl::internal::WriteKeTimeStampBundle(storage.data());

  const auto interrupt_time = static_cast<uint64_t>(bundle->interrupt_time);
  const auto system_time = static_cast<uint64_t>(bundle->system_time);
  const auto tick_count = static_cast<uint32_t>(bundle->tick_count);
  const auto expected_tick_count = static_cast<uint32_t>(
      std::min<uint64_t>(interrupt_time / 10000, std::numeric_limits<uint32_t>::max()));

  CHECK(interrupt_time > 0);
  CHECK(system_time > interrupt_time);
  CHECK(tick_count == expected_tick_count);
  CHECK(returned_tick_count == tick_count);
  CHECK(static_cast<uint32_t>(bundle->padding) == 0);
}
