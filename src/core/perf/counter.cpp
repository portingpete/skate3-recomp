/**
 * @file        core/perf/counter.cpp
 * @brief       Performance counter registry implementation
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#include <rex/perf/counter.h>

#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

REXCVAR_DEFINE_STRING(perf_log_csv, "", "Perf",
                      "Path to write per-frame CSV log (empty = disabled)");

namespace rex::perf {

namespace {

constexpr size_t kNumCounters = static_cast<size_t>(CounterId::kCount);

std::array<std::atomic<int64_t>, kNumCounters> g_counters{};
std::array<std::atomic<int64_t>, kNumCounters> g_snapshot{};

constexpr const char* kCounterNames[] = {
    "frame_time_us",
    "fps",
    "draw_calls",
    "command_buffer_stalls",
    "vertices_processed",
    "async_pipeline_skipped_draws",
    "async_pipeline_pending_draws",
    "async_pipeline_failed_draws",
    "d3d12_submissions",
    "d3d12_present_calls",
    "memexport_readback_full",
    "memexport_readback_fast",
    "memexport_readback_fallback",
    "guest_function_dispatch_us",
    "d3d12_submission_wait_us",
    "d3d12_present_us",
    "memexport_readback_us",
    "xma_frames_decoded",
    "audio_frame_latency_us",
    "buffer_queue_depth",
    "functions_dispatched",
    "interrupt_dispatches",
    "active_threads",
    "apc_queue_depth",
    "critical_region_contentions",
    "texture_cache_hits",
    "texture_cache_misses",
    "pipeline_cache_hits",
    "pipeline_cache_misses",
};
static_assert(std::size(kCounterNames) == kNumCounters, "kCounterNames must match CounterId enum");

// Gauge counters are snapshotted but NOT zeroed each frame.
// Accumulators (everything else) are zeroed after snapshot.
constexpr bool kIsGauge[] = {
    false,  // kFrameTimeUs       (set each frame)
    false,  // kFps               (set each frame)
    false,  // kDrawCalls
    false,  // kCommandBufferStalls
    false,  // kVerticesProcessed
    false,  // kAsyncPipelineSkippedDraws
    false,  // kAsyncPipelinePendingDraws
    false,  // kAsyncPipelineFailedDraws
    false,  // kD3D12Submissions
    false,  // kD3D12PresentCalls
    false,  // kMemexportReadbackFull
    false,  // kMemexportReadbackFast
    false,  // kMemexportReadbackFallback
    false,  // kGuestFunctionDispatchUs
    false,  // kD3D12SubmissionWaitUs
    false,  // kD3D12PresentUs
    false,  // kMemexportReadbackUs
    false,  // kXmaFramesDecoded
    false,  // kAudioFrameLatencyUs
    false,  // kBufferQueueDepth  (set each frame)
    false,  // kFunctionsDispatched
    false,  // kInterruptDispatches
    true,   // kActiveThreads     (inc/dec over lifetime)
    false,  // kApcQueueDepth
    true,   // kCriticalRegionContentions (running total)
    false,  // kTextureCacheHits
    false,  // kTextureCacheMisses
    false,  // kPipelineCacheHits
    false,  // kPipelineCacheMisses
};
static_assert(std::size(kIsGauge) == kNumCounters, "kIsGauge must match CounterId enum");

// CSV state
std::FILE* g_csv_file = nullptr;
std::string g_csv_path;
uint64_t g_csv_frame_count = 0;
uint64_t g_csv_start_tick = 0;

}  // anonymous namespace

const char* CounterName(CounterId id) {
  auto idx = static_cast<size_t>(id);
  if (idx < kNumCounters)
    return kCounterNames[idx];
  return "unknown";
}

void SetCounter(CounterId id, int64_t value) {
  g_counters[static_cast<size_t>(id)].store(value, std::memory_order_relaxed);
}

void IncrementCounter(CounterId id, int64_t delta) {
  g_counters[static_cast<size_t>(id)].fetch_add(delta, std::memory_order_relaxed);
}

void AddCounterDurationSince(CounterId id, uint64_t start_tick) {
  const uint64_t end_tick = rex::chrono::Clock::QueryHostTickCount();
  const uint64_t freq = rex::chrono::Clock::QueryHostTickFrequency();
  if (freq == 0 || end_tick <= start_tick) {
    return;
  }
  IncrementCounter(id, static_cast<int64_t>((end_tick - start_tick) * UINT64_C(1000000) / freq));
}

int64_t GetCounter(CounterId id) {
  return g_counters[static_cast<size_t>(id)].load(std::memory_order_relaxed);
}

void ResetFrameCounters() {
  for (size_t i = 0; i < kNumCounters; ++i) {
    if (kIsGauge[i]) {
      // Gauges: snapshot the current value, don't zero
      g_snapshot[i].store(g_counters[i].load(std::memory_order_relaxed), std::memory_order_relaxed);
    } else {
      // Accumulators: snapshot and zero for next frame
      g_snapshot[i].store(g_counters[i].exchange(0, std::memory_order_relaxed),
                          std::memory_order_relaxed);
    }
  }
}

int64_t GetSnapshotCounter(CounterId id) {
  return g_snapshot[static_cast<size_t>(id)].load(std::memory_order_relaxed);
}

void Init() {
  for (auto& c : g_counters)
    c.store(0, std::memory_order_relaxed);
  for (auto& s : g_snapshot)
    s.store(0, std::memory_order_relaxed);
}

void SetCsvLogPath(const std::string& path) {
  if (g_csv_file) {
    std::fflush(g_csv_file);
    std::fclose(g_csv_file);
    g_csv_file = nullptr;
  }
  g_csv_path = path;
  g_csv_frame_count = 0;
  g_csv_start_tick = 0;

  if (path.empty())
    return;

  g_csv_file = rex::filesystem::OpenFile(rex::to_path(path), "w");
  if (!g_csv_file) {
    REXLOG_WARN("perf: failed to open CSV log: {}", path);
    g_csv_path.clear();
    return;
  }
  g_csv_start_tick = rex::chrono::Clock::QueryHostTickCount();

  // Write header
  std::fputs("frame_index,elapsed_us", g_csv_file);
  for (size_t i = 0; i < kNumCounters; ++i) {
    std::fputc(',', g_csv_file);
    std::fputs(kCounterNames[i], g_csv_file);
  }
  std::fputc('\n', g_csv_file);
}

void ConfigureCsvLogPathFromCvar() {
  SetCsvLogPath(REXCVAR_GET(perf_log_csv));
}

void WriteCsvFrame() {
  if (!g_csv_file)
    return;

  uint64_t elapsed_us = 0;
  if (g_csv_start_tick) {
    uint64_t now = rex::chrono::Clock::QueryHostTickCount();
    uint64_t freq = rex::chrono::Clock::QueryHostTickFrequency();
    if (freq != 0) {
      elapsed_us = (now - g_csv_start_tick) * UINT64_C(1000000) / freq;
    }
  }

  std::fprintf(g_csv_file, "%llu,%llu", static_cast<unsigned long long>(g_csv_frame_count),
               static_cast<unsigned long long>(elapsed_us));
  for (size_t i = 0; i < kNumCounters; ++i) {
    std::fputc(',', g_csv_file);
    std::fprintf(g_csv_file, "%lld",
                 static_cast<long long>(g_snapshot[i].load(std::memory_order_relaxed)));
  }
  std::fputc('\n', g_csv_file);

  if (++g_csv_frame_count % 60 == 0) {
    std::fflush(g_csv_file);
  }
}

void FlushCsv() {
  if (g_csv_file) {
    std::fflush(g_csv_file);
    std::fclose(g_csv_file);
    g_csv_file = nullptr;
  }
  g_csv_path.clear();
  g_csv_frame_count = 0;
  g_csv_start_tick = 0;
}

ScopedCounterDuration::ScopedCounterDuration(CounterId id)
    : id_(id), start_tick_(rex::chrono::Clock::QueryHostTickCount()) {}

ScopedCounterDuration::~ScopedCounterDuration() {
  AddCounterDurationSince(id_, start_tick_);
}

}  // namespace rex::perf
