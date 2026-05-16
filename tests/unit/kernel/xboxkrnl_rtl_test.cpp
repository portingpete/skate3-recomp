#include <catch2/catch_test_macros.hpp>

#include <rex/types.h>

namespace rex::kernel::xboxkrnl {
u32 RtlUpcaseUnicodeChar_entry(u32 in);
u32 RtlDowncaseUnicodeChar_entry(u32 in);
}  // namespace rex::kernel::xboxkrnl

TEST_CASE("Unicode case helpers map ASCII letters and preserve other codepoints",
          "[kernel][xboxkrnl][rtl]") {
  CHECK(rex::kernel::xboxkrnl::RtlUpcaseUnicodeChar_entry('a') == 'A');
  CHECK(rex::kernel::xboxkrnl::RtlUpcaseUnicodeChar_entry('Z') == 'Z');
  CHECK(rex::kernel::xboxkrnl::RtlUpcaseUnicodeChar_entry(0x00E9) == 0x00E9);

  CHECK(rex::kernel::xboxkrnl::RtlDowncaseUnicodeChar_entry('A') == 'a');
  CHECK(rex::kernel::xboxkrnl::RtlDowncaseUnicodeChar_entry('z') == 'z');
  CHECK(rex::kernel::xboxkrnl::RtlDowncaseUnicodeChar_entry(0x00C9) == 0x00C9);
}
