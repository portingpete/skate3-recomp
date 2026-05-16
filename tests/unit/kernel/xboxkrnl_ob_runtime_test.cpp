#include <catch2/catch_test_macros.hpp>

#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/util/object_table.h>
#include <rex/system/xevent.h>
#include <rex/system/xobject.h>
#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::kernel::xboxkrnl {
void ObReferenceObject_entry(mapped_void object_ptr);
u32 ObDereferenceObject_entry(u32 native_ptr);
}  // namespace rex::kernel::xboxkrnl

TEST_CASE("ObReferenceObject retains valid direct object handles",
          "[kernel][xboxkrnl][ob][runtime]") {
  rex::Runtime runtime({}, {}, {}, {});
  rex::RuntimeConfig config;
  config.tool_mode = true;
  REQUIRE(runtime.Setup(std::move(config)) == 0);

  auto event = rex::system::make_object<rex::system::XEvent>(runtime.kernel_state());
  event->Initialize(/*manual_reset=*/true, /*initial_state=*/false);

  const rex::X_HANDLE handle = event->handle();
  const u32 guest_object = event->guest_object();
  auto* host_object = runtime.memory()->TranslateVirtual<void*>(guest_object);

  rex::kernel::xboxkrnl::ObReferenceObject_entry(mapped_void(host_object, guest_object));
  CHECK(rex::kernel::xboxkrnl::ObDereferenceObject_entry(guest_object) == 0);
  CHECK(runtime.kernel_state()->object_table()->LookupObject<rex::system::XObject>(handle) !=
        nullptr);

  CHECK(runtime.kernel_state()->object_table()->ReleaseHandle(handle) == 0);
  CHECK(runtime.kernel_state()->object_table()->LookupObject<rex::system::XObject>(handle) ==
        nullptr);
}
