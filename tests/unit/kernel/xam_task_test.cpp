#include <catch2/catch_test_macros.hpp>

#include <rex/system/util/object_table.h>
#include <rex/system/xobject.h>
#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::kernel::xam {
u32 XamTaskCloseHandleForObjectTable(rex::system::util::ObjectTable* object_table, u32 handle);
}  // namespace rex::kernel::xam

namespace {

class TestTaskHandleObject final : public rex::system::XObject {
 public:
  TestTaskHandleObject() : XObject(Type::Undefined) {}
};

}  // namespace

TEST_CASE("XamTaskCloseHandle releases object-table handles with boolean result",
          "[kernel][xam_task]") {
  constexpr rex::X_STATUS kSuccess = 0;
  rex::system::util::ObjectTable object_table;
  auto* object = new TestTaskHandleObject();
  rex::system::object_ref<TestTaskHandleObject> object_owner(object);

  rex::X_HANDLE handle = 0;
  REQUIRE(object_table.AddHandle(object, &handle) == kSuccess);
  REQUIRE(handle != 0);

  CHECK(rex::kernel::xam::XamTaskCloseHandleForObjectTable(&object_table, handle) == 1);
  CHECK(object_table.LookupObject<rex::system::XObject>(handle) == nullptr);

  CHECK(rex::kernel::xam::XamTaskCloseHandleForObjectTable(&object_table, handle) == 0);
  CHECK(rex::kernel::xam::XamTaskCloseHandleForObjectTable(&object_table, 12345) == 0);
}
