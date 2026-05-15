#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include <rex/audio/audio_driver.h>
#include <rex/audio/audio_system.h>
#include <rex/system/export_resolver.h>
#include <rex/system/function_dispatcher.h>
#include <rex/thread.h>

#include "../test_memory.h"

namespace {

using rex::X_STATUS;

class TestAudioDriver final : public rex::audio::AudioDriver {
 public:
  explicit TestAudioDriver(rex::memory::Memory* memory) : AudioDriver(memory) {}

  void SubmitFrame(uint32_t samples_ptr) override { last_samples_ptr = samples_ptr; }

  uint32_t last_samples_ptr = 0;
};

class TestAudioSystem final : public rex::audio::AudioSystem {
 public:
  explicit TestAudioSystem(rex::runtime::FunctionDispatcher* dispatcher) : AudioSystem(dispatcher) {}

  X_STATUS create_status = X_STATUS_SUCCESS;
  bool semaphore_was_signaled_during_create = false;

 protected:
  X_STATUS CreateDriver(size_t index, rex::thread::Semaphore* semaphore,
                        rex::audio::AudioDriver** out_driver) override {
    (void)index;
    semaphore_was_signaled_during_create =
        rex::thread::Wait(semaphore, false, std::chrono::milliseconds(0)) ==
        rex::thread::WaitResult::kSuccess;
    if (XFAILED(create_status)) {
      *out_driver = nullptr;
      return create_status;
    }
    *out_driver = new TestAudioDriver(memory());
    return X_STATUS_SUCCESS;
  }

  void DestroyDriver(rex::audio::AudioDriver* driver) override { delete driver; }
};

struct TestAudioRuntime {
  TestAudioRuntime()
      : memory(rex::test::GetTestMemory()), dispatcher(&memory, &export_resolver) {}

  rex::memory::Memory& memory;
  rex::runtime::ExportResolver export_resolver;
  rex::runtime::FunctionDispatcher dispatcher;
};

}  // namespace

TEST_CASE("Audio client registration installs callback before queued wake signals",
          "[audio][audio_system]") {
  TestAudioRuntime runtime;
  TestAudioSystem audio(&runtime.dispatcher);

  size_t index = 99;
  CHECK(audio.RegisterClient(0x82000000, 0x40001000, &index) == X_STATUS_SUCCESS);
  CHECK(index == 0);
  CHECK_FALSE(audio.semaphore_was_signaled_during_create);

  audio.UnregisterClient(index);
}

TEST_CASE("Audio client registration falls back to a silent driver when output is unavailable",
          "[audio][audio_system]") {
  TestAudioRuntime runtime;
  TestAudioSystem audio(&runtime.dispatcher);
  audio.create_status = X_STATUS_UNSUCCESSFUL;

  size_t index = 99;
  REQUIRE(audio.RegisterClient(0x82000000, 0x40001000, &index) == X_STATUS_SUCCESS);
  REQUIRE(index == 0);

  audio.SubmitFrame(index, 0x40002000);
  audio.UnregisterClient(index);
}
