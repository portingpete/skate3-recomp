#include <catch2/catch_test_macros.hpp>

#include <rex/audio/sdl/sdl_audio_driver.h>
#include <rex/perf/counter.h>
#include <rex/thread.h>

namespace {

class TestSdlAudioDriver final : public rex::audio::sdl::SDLAudioDriver {
 public:
  using SDLAudioDriver::SDLAudioDriver;

  void QueueOwnedFrame(float* frame) { frames_queued_.push(frame); }
};

}  // namespace

TEST_CASE("SDL audio shutdown updates buffer queue depth after discarding frames", "[audio][sdl]") {
  rex::perf::Init();
  auto semaphore = rex::thread::Semaphore::Create(0, 16);
  TestSdlAudioDriver driver(nullptr, semaphore.get());

  auto* frame = new float[1]();
  driver.QueueOwnedFrame(frame);
  rex::perf::SetCounter(rex::perf::CounterId::kBufferQueueDepth, 1);

  driver.Shutdown();

  CHECK(rex::perf::GetCounter(rex::perf::CounterId::kBufferQueueDepth) == 0);
}