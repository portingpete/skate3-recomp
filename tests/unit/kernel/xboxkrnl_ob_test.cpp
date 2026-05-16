#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::kernel::xboxkrnl {
u32 ObIsTitleObject_entry(mapped_void object_ptr);
void ObReferenceObject_entry(mapped_void object_ptr);
}  // namespace rex::kernel::xboxkrnl

TEST_CASE("ObIsTitleObject returns a normalized pointer predicate", "[kernel][xboxkrnl][ob]") {
  CHECK(rex::kernel::xboxkrnl::ObIsTitleObject_entry(mapped_void(nullptr)) == 0);
  CHECK(rex::kernel::xboxkrnl::ObIsTitleObject_entry(
            mapped_void(reinterpret_cast<void*>(uintptr_t{0x1}), 0x82000000)) == 1);
}

TEST_CASE("ObReferenceObject tolerates null direct object pointers", "[kernel][xboxkrnl][ob]") {
  rex::kernel::xboxkrnl::ObReferenceObject_entry(mapped_void(nullptr));
}
