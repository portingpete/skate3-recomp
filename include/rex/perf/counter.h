/**
 * @file        perf/counter.h
 * @brief       Performance counter registry and profiler
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifdef REXGLUE_ENABLE_PROFILING
#include <tracy/Tracy.hpp>
#endif

namespace rex::perf {

enum class CounterId : uint16_t {
  // Frame
  kFrameTimeUs,
  kFps,

  // GPU
  kDrawCalls,
  kCommandBufferStalls,
  kVerticesProcessed,
  kAsyncPipelineSkippedDraws,
  kAsyncPipelinePendingDraws,
  kAsyncPipelineFailedDraws,
  kD3D12Submissions,
  kD3D12PresentCalls,
  kMemexportReadbackFull,
  kMemexportReadbackFast,
  kMemexportReadbackFallback,
  kGuestFunctionDispatchUs,
  kGuestKernelWaitUs,
  kD3D12SubmissionWaitUs,
  kD3D12PresentUs,
  kMemexportReadbackUs,

  // Audio
  kXmaFramesDecoded,
  kAudioFrameLatencyUs,
  kBufferQueueDepth,

  // Dispatch
  kFunctionsDispatched,
  kInterruptDispatches,

  // Threading
  kActiveThreads,
  kApcQueueDepth,
  kCriticalRegionContentions,

  // Caches
  kTextureCacheHits,
  kTextureCacheMisses,
  kPipelineCacheHits,
  kPipelineCacheMisses,

  kCount  // sentinel -- must be last
};

// Returns human-readable name for a counter (e.g. "frame_time_us")
const char* CounterName(CounterId id);

// Set a counter to an absolute value
void SetCounter(CounterId id, int64_t value);

// Atomically add to a counter
void IncrementCounter(CounterId id, int64_t delta = 1);

// Add the elapsed time since start_tick to a microsecond duration counter.
void AddCounterDurationSince(CounterId id, uint64_t start_tick);

// Read a counter's current live value
int64_t GetCounter(CounterId id);

// Snapshot current values into the read buffer and zero the live counters.
// Called once per frame by Profiler::Flip().
void ResetFrameCounters();

// Read a counter from the last-frame snapshot (stable between frames).
int64_t GetSnapshotCounter(CounterId id);

// Initialize the counter system (zeroes everything). Safe to call multiple times.
void Init();

// CSV logging
void ConfigureCsvLogPathFromCvar();
void SetCsvLogPath(const std::string& path);
void WriteCsvFrame();
void FlushCsv();

struct GuestFunctionProfileEntry {
  uint32_t address = 0;
  std::string symbol;
  uint64_t calls = 0;
  uint64_t inclusive_us = 0;
  uint64_t exclusive_us = 0;
  uint64_t blocking_wait_us = 0;
  uint64_t active_exclusive_us = 0;
  uint32_t spin_hint_sites = 0;
};

struct GuestIndirectCallTargetProfileEntry {
  uint32_t source_address = 0;
  std::string source_symbol;
  uint32_t call_site = 0;
  uint32_t target_address = 0;
  uint64_t calls = 0;
  uint64_t fast_path_hits = 0;
  uint64_t fallback_hits = 0;
};

void AddGuestFunctionDurationUs(uint32_t address, const char* symbol, uint64_t inclusive_us,
                                uint64_t exclusive_us, uint64_t blocking_wait_us = 0,
                                uint32_t spin_hint_sites = 0);
void AddGuestKernelWaitDurationUs(uint64_t duration_us);
void AddGuestIndirectCallTarget(uint32_t source_address, const char* source_symbol,
                                uint32_t call_site, uint32_t target_address,
                                bool fast_path_hit);

// Returns the current top entries and clears the frame-local accumulator.
std::vector<GuestFunctionProfileEntry> SnapshotGuestFunctionProfile(size_t max_entries,
                                                                    uint64_t min_exclusive_us);
std::vector<GuestIndirectCallTargetProfileEntry> SnapshotGuestIndirectCallTargetProfile(
    size_t max_entries);

class ScopedCounterDuration {
 public:
  explicit ScopedCounterDuration(CounterId id);
  ~ScopedCounterDuration();

  ScopedCounterDuration(const ScopedCounterDuration&) = delete;
  ScopedCounterDuration& operator=(const ScopedCounterDuration&) = delete;

 private:
  CounterId id_;
  uint64_t start_tick_;
};

class ScopedGuestFunctionProfile {
 public:
  ScopedGuestFunctionProfile(uint32_t address, const char* symbol, uint32_t spin_hint_sites = 0);
  ~ScopedGuestFunctionProfile();

  ScopedGuestFunctionProfile(const ScopedGuestFunctionProfile&) = delete;
  ScopedGuestFunctionProfile& operator=(const ScopedGuestFunctionProfile&) = delete;

 private:
  bool active_ = false;
  uint32_t address_ = 0;
  const char* symbol_ = nullptr;
  uint64_t start_tick_ = 0;
  size_t stack_index_ = 0;
  uint64_t stack_token_ = 0;
  uint64_t generation_ = 0;
  uint32_t spin_hint_sites_ = 0;
};

class ScopedGuestKernelWaitProfile {
 public:
  ScopedGuestKernelWaitProfile();
  ~ScopedGuestKernelWaitProfile();

  ScopedGuestKernelWaitProfile(const ScopedGuestKernelWaitProfile&) = delete;
  ScopedGuestKernelWaitProfile& operator=(const ScopedGuestKernelWaitProfile&) = delete;

 private:
  uint64_t start_tick_ = 0;
};

// Profiler -- coordinates Tracy frame marks and counter snapshots.
// Moved here from rex::debug to consolidate all perf code under rex::perf.
class Profiler {
 public:
  // Call once at runtime startup to enable Tracy's network threads.
  // CLI tools should skip this to avoid socket listeners entirely.
  static void Startup() {
#ifdef REXGLUE_ENABLE_PROFILING
    tracy::StartupProfiler();
#endif
#ifdef REXGLUE_ENABLE_PERF_COUNTERS
    Init();
    ConfigureCsvLogPathFromCvar();
#endif
  }
  static void OnThreadEnter(const char* name = nullptr) {
#ifdef REXGLUE_ENABLE_PROFILING
    if (name)
      tracy::SetThreadName(name);
#else
    (void)name;
#endif
  }
  static void OnThreadExit() {}
  static void ThreadEnter(const char* name = nullptr) { OnThreadEnter(name); }
  static void ThreadExit() {}
  static void Flip() {
#ifdef REXGLUE_ENABLE_PROFILING
    FrameMark;
#endif
#ifdef REXGLUE_ENABLE_PERF_COUNTERS
    ResetFrameCounters();
    WriteCsvFrame();
#endif
  }
  static void Flush() {}
  static void Shutdown() {
#ifdef REXGLUE_ENABLE_PROFILING
    tracy::ShutdownProfiler();
#endif
#ifdef REXGLUE_ENABLE_PERF_COUNTERS
    FlushCsv();
#endif
  }
  static bool is_enabled() {
#ifdef REXGLUE_ENABLE_PROFILING
    return tracy::IsProfilerStarted();
#else
    return false;
#endif
  }
};

}  // namespace rex::perf

// Perf counter macros -- compile to no-ops when counters are disabled.
#ifdef REXGLUE_ENABLE_PERF_COUNTERS

// Generic helpers for easily adding new counters
#define PERF_counter_set(id, value) rex::perf::SetCounter(rex::perf::CounterId::id, value)
#define PERF_counter_inc(id) rex::perf::IncrementCounter(rex::perf::CounterId::id)
#define PERF_counter_add(id, delta) rex::perf::IncrementCounter(rex::perf::CounterId::id, delta)
#define REX_PERF_CONCAT_INNER(a, b) a##b
#define REX_PERF_CONCAT(a, b) REX_PERF_CONCAT_INNER(a, b)
#define PERF_counter_duration_scope(id)                                                    \
  rex::perf::ScopedCounterDuration REX_PERF_CONCAT(_rex_perf_duration_scope_, __LINE__)( \
      rex::perf::CounterId::id)

// Purpose-specific macros so callsites stay clean
#define PROFILE_FRAME_TIME_US(value) PERF_counter_set(kFrameTimeUs, value)
#define PROFILE_FPS(value) PERF_counter_set(kFps, value)
#define PROFILE_FUNCTION_DISPATCHED() PERF_counter_inc(kFunctionsDispatched)
#define PROFILE_INTERRUPT_DISPATCHED() PERF_counter_inc(kInterruptDispatches)
#define PROFILE_XMA_FRAME_DECODED() PERF_counter_inc(kXmaFramesDecoded)
#define PROFILE_DRAW_CALL() PERF_counter_inc(kDrawCalls)
#define PROFILE_VERTICES(n) PERF_counter_add(kVerticesProcessed, n)
#define PROFILE_CMD_BUFFER_STALL() PERF_counter_inc(kCommandBufferStalls)
#define PROFILE_ASYNC_PIPELINE_SKIPPED_DRAW() PERF_counter_inc(kAsyncPipelineSkippedDraws)
#define PROFILE_ASYNC_PIPELINE_PENDING_DRAW() PERF_counter_inc(kAsyncPipelinePendingDraws)
#define PROFILE_ASYNC_PIPELINE_FAILED_DRAW() PERF_counter_inc(kAsyncPipelineFailedDraws)
#define PROFILE_D3D12_SUBMISSION() PERF_counter_inc(kD3D12Submissions)
#define PROFILE_D3D12_PRESENT_CALL() PERF_counter_inc(kD3D12PresentCalls)
#define PROFILE_MEMEXPORT_READBACK_FULL() PERF_counter_inc(kMemexportReadbackFull)
#define PROFILE_MEMEXPORT_READBACK_FAST() PERF_counter_inc(kMemexportReadbackFast)
#define PROFILE_MEMEXPORT_READBACK_FALLBACK() PERF_counter_inc(kMemexportReadbackFallback)
#define PROFILE_GUEST_FUNCTION_DISPATCH_SCOPE() \
  PERF_counter_duration_scope(kGuestFunctionDispatchUs)
#define PROFILE_GUEST_KERNEL_WAIT_SCOPE()                                                \
  rex::perf::ScopedGuestKernelWaitProfile REX_PERF_CONCAT(_rex_perf_guest_wait_scope_, \
                                                          __LINE__)
#define PROFILE_GUEST_INDIRECT_CALL_TARGET(source_address, source_symbol, call_site,     \
                                           target_address, fast_path_hit)                 \
  rex::perf::AddGuestIndirectCallTarget(source_address, source_symbol, call_site,         \
                                        target_address, fast_path_hit)
#define REX_PERF_GUEST_FUNCTION_SCOPE_2(address, symbol)                                  \
  rex::perf::ScopedGuestFunctionProfile REX_PERF_CONCAT(_rex_perf_guest_func_scope_,       \
                                                        __LINE__)(address, symbol)
#define REX_PERF_GUEST_FUNCTION_SCOPE_3(address, symbol, spin_hint_sites)                 \
  rex::perf::ScopedGuestFunctionProfile REX_PERF_CONCAT(_rex_perf_guest_func_scope_,       \
                                                        __LINE__)(address, symbol,          \
                                                                  spin_hint_sites)
#define REX_PERF_SELECT_GUEST_FUNCTION_SCOPE(_1, _2, _3, NAME, ...) NAME
#define PROFILE_GUEST_FUNCTION_SCOPE(...)                                                 \
  REX_PERF_SELECT_GUEST_FUNCTION_SCOPE(__VA_ARGS__, REX_PERF_GUEST_FUNCTION_SCOPE_3,       \
                                       REX_PERF_GUEST_FUNCTION_SCOPE_2)(__VA_ARGS__)
#define PROFILE_D3D12_SUBMISSION_WAIT_SCOPE() PERF_counter_duration_scope(kD3D12SubmissionWaitUs)
#define PROFILE_D3D12_PRESENT_SCOPE() PERF_counter_duration_scope(kD3D12PresentUs)
#define PROFILE_MEMEXPORT_READBACK_SCOPE() PERF_counter_duration_scope(kMemexportReadbackUs)
#define PROFILE_AUDIO_LATENCY_US(value) PERF_counter_set(kAudioFrameLatencyUs, value)
#define PROFILE_BUFFER_QUEUE_DEPTH(value) PERF_counter_set(kBufferQueueDepth, value)
#define PROFILE_THREAD_CREATED() PERF_counter_inc(kActiveThreads)
#define PROFILE_THREAD_EXITED() PERF_counter_add(kActiveThreads, -1)
#define PROFILE_APC_QUEUE_DEPTH(value) PERF_counter_set(kApcQueueDepth, value)
#define PROFILE_CRITICAL_REGION_CONTENTION() PERF_counter_inc(kCriticalRegionContentions)
#define PROFILE_TEXTURE_CACHE_HIT() PERF_counter_inc(kTextureCacheHits)
#define PROFILE_TEXTURE_CACHE_MISS() PERF_counter_inc(kTextureCacheMisses)
#define PROFILE_PIPELINE_CACHE_HIT() PERF_counter_inc(kPipelineCacheHits)
#define PROFILE_PIPELINE_CACHE_MISS() PERF_counter_inc(kPipelineCacheMisses)

#else

#define PERF_counter_set(id, value)
#define PERF_counter_inc(id)
#define PERF_counter_add(id, delta)
#define PERF_counter_duration_scope(id)

#define PROFILE_FRAME_TIME_US(value)
#define PROFILE_FPS(value)
#define PROFILE_FUNCTION_DISPATCHED()
#define PROFILE_INTERRUPT_DISPATCHED()
#define PROFILE_XMA_FRAME_DECODED()
#define PROFILE_DRAW_CALL()
#define PROFILE_VERTICES(n)
#define PROFILE_CMD_BUFFER_STALL()
#define PROFILE_ASYNC_PIPELINE_SKIPPED_DRAW()
#define PROFILE_ASYNC_PIPELINE_PENDING_DRAW()
#define PROFILE_ASYNC_PIPELINE_FAILED_DRAW()
#define PROFILE_D3D12_SUBMISSION()
#define PROFILE_D3D12_PRESENT_CALL()
#define PROFILE_MEMEXPORT_READBACK_FULL()
#define PROFILE_MEMEXPORT_READBACK_FAST()
#define PROFILE_MEMEXPORT_READBACK_FALLBACK()
#define PROFILE_GUEST_FUNCTION_DISPATCH_SCOPE()
#define PROFILE_GUEST_KERNEL_WAIT_SCOPE()
#define PROFILE_GUEST_INDIRECT_CALL_TARGET(source_address, source_symbol, call_site, target_address, \
                                           fast_path_hit)
#define PROFILE_GUEST_FUNCTION_SCOPE(...)
#define PROFILE_D3D12_SUBMISSION_WAIT_SCOPE()
#define PROFILE_D3D12_PRESENT_SCOPE()
#define PROFILE_MEMEXPORT_READBACK_SCOPE()
#define PROFILE_AUDIO_LATENCY_US(value)
#define PROFILE_BUFFER_QUEUE_DEPTH(value)
#define PROFILE_THREAD_CREATED()
#define PROFILE_THREAD_EXITED()
#define PROFILE_APC_QUEUE_DEPTH(value)
#define PROFILE_CRITICAL_REGION_CONTENTION()
#define PROFILE_TEXTURE_CACHE_HIT()
#define PROFILE_TEXTURE_CACHE_MISS()
#define PROFILE_PIPELINE_CACHE_HIT()
#define PROFILE_PIPELINE_CACHE_MISS()

#endif
