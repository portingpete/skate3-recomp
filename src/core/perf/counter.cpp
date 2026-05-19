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

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

REXCVAR_DEFINE_STRING(perf_log_csv, "", "Perf",
                      "Path to write per-frame CSV log (empty = disabled)");
REXCVAR_DEFINE_INT32(perf_guest_functions_top_n, 0, "Perf",
                     "Write the top N generated guest functions to a sidecar perf CSV");
REXCVAR_DEFINE_INT64(perf_guest_functions_min_exclusive_us, 0, "Perf",
                     "Minimum active exclusive guest-function time, in microseconds, for sidecar perf CSV");
REXCVAR_DEFINE_INT32(perf_guest_indirect_targets_top_n, 0, "Perf",
                     "Write the top N generated indirect-call targets to a sidecar perf CSV");

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
    "guest_kernel_wait_us",
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
    false,  // kGuestKernelWaitUs
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

struct GuestFunctionProfileTotals {
  std::string symbol;
  uint64_t calls = 0;
  uint64_t inclusive_us = 0;
  uint64_t exclusive_us = 0;
  uint64_t blocking_wait_us = 0;
  uint32_t spin_hint_sites = 0;
};

struct GuestFunctionStackEntry {
  uint32_t address = 0;
  const char* symbol = nullptr;
  uint64_t start_tick = 0;
  uint64_t child_us = 0;
  uint64_t blocking_wait_us = 0;
  uint64_t token = 0;
  uint32_t spin_hint_sites = 0;
};

struct GuestIndirectCallTargetKey {
  uint32_t source_address = 0;
  uint32_t call_site = 0;
  uint32_t target_address = 0;

  bool operator==(const GuestIndirectCallTargetKey& other) const {
    return source_address == other.source_address && call_site == other.call_site &&
           target_address == other.target_address;
  }
};

struct GuestIndirectCallTargetKeyHash {
  size_t operator()(const GuestIndirectCallTargetKey& key) const {
    size_t hash = std::hash<uint32_t>{}(key.source_address);
    hash ^= std::hash<uint32_t>{}(key.call_site) + 0x9E3779B9u + (hash << 6) + (hash >> 2);
    hash ^= std::hash<uint32_t>{}(key.target_address) + 0x9E3779B9u + (hash << 6) + (hash >> 2);
    return hash;
  }
};

struct GuestIndirectCallTargetProfileTotals {
  std::string source_symbol;
  uint64_t calls = 0;
  uint64_t fast_path_hits = 0;
  uint64_t fallback_hits = 0;
};

std::mutex g_guest_function_profile_mutex;
std::unordered_map<uint32_t, GuestFunctionProfileTotals> g_guest_function_profile;
std::atomic<bool> g_guest_function_profile_enabled{false};
std::atomic<uint64_t> g_guest_function_profile_generation{1};
std::FILE* g_guest_function_csv_file = nullptr;
std::string g_guest_function_csv_path;

std::mutex g_guest_indirect_call_profile_mutex;
std::unordered_map<GuestIndirectCallTargetKey, GuestIndirectCallTargetProfileTotals,
                   GuestIndirectCallTargetKeyHash>
    g_guest_indirect_call_profile;
std::atomic<bool> g_guest_indirect_call_profile_enabled{false};
std::FILE* g_guest_indirect_call_csv_file = nullptr;
std::string g_guest_indirect_call_csv_path;

thread_local std::vector<GuestFunctionStackEntry> g_guest_function_stack;
thread_local uint64_t g_guest_function_stack_next_token = 0;

uint64_t DurationUsSince(uint64_t start_tick) {
  const uint64_t end_tick = rex::chrono::Clock::QueryHostTickCount();
  const uint64_t freq = rex::chrono::Clock::QueryHostTickFrequency();
  if (freq == 0 || end_tick <= start_tick) {
    return 0;
  }
  return (end_tick - start_tick) * UINT64_C(1000000) / freq;
}

void ResetGuestFunctionProfile() {
  std::lock_guard lock(g_guest_function_profile_mutex);
  g_guest_function_profile.clear();
}

void ResetGuestIndirectCallProfile() {
  std::lock_guard lock(g_guest_indirect_call_profile_mutex);
  g_guest_indirect_call_profile.clear();
}

std::string BuildGuestFunctionCsvPath(const std::string& path) {
  return path + ".guest_functions.csv";
}

std::string BuildGuestIndirectCallCsvPath(const std::string& path) {
  return path + ".guest_indirect_targets.csv";
}

void WriteCsvCell(std::FILE* file, std::string_view value) {
  const bool needs_quotes = value.find_first_of(",\"\r\n") != std::string_view::npos;
  if (!needs_quotes) {
    std::fwrite(value.data(), 1, value.size(), file);
    return;
  }

  std::fputc('"', file);
  for (const char ch : value) {
    if (ch == '"') {
      std::fputc('"', file);
    }
    std::fputc(ch, file);
  }
  std::fputc('"', file);
}

void CloseGuestFunctionCsv() {
  g_guest_function_profile_enabled.store(false, std::memory_order_relaxed);
  g_guest_function_profile_generation.fetch_add(1, std::memory_order_relaxed);
  if (g_guest_function_csv_file) {
    std::fflush(g_guest_function_csv_file);
    std::fclose(g_guest_function_csv_file);
    g_guest_function_csv_file = nullptr;
  }
  g_guest_function_csv_path.clear();
  ResetGuestFunctionProfile();
}

void CloseGuestIndirectCallCsv() {
  g_guest_indirect_call_profile_enabled.store(false, std::memory_order_relaxed);
  if (g_guest_indirect_call_csv_file) {
    std::fflush(g_guest_indirect_call_csv_file);
    std::fclose(g_guest_indirect_call_csv_file);
    g_guest_indirect_call_csv_file = nullptr;
  }
  g_guest_indirect_call_csv_path.clear();
  ResetGuestIndirectCallProfile();
}

void ConfigureGuestFunctionCsv(const std::string& path) {
  CloseGuestFunctionCsv();

  const int32_t top_n = REXCVAR_GET(perf_guest_functions_top_n);
  if (path.empty() || top_n <= 0) {
    return;
  }

  g_guest_function_csv_path = BuildGuestFunctionCsvPath(path);
  g_guest_function_csv_file =
      rex::filesystem::OpenFile(rex::to_path(g_guest_function_csv_path), "w");
  if (!g_guest_function_csv_file) {
    REXLOG_WARN("perf: failed to open guest-function CSV log: {}", g_guest_function_csv_path);
    g_guest_function_csv_path.clear();
    return;
  }

  std::fputs("frame_index,elapsed_us,rank,guest_address,symbol,calls,inclusive_us,exclusive_us,"
             "blocking_wait_us,active_exclusive_us,spin_hint_sites\n",
             g_guest_function_csv_file);
  g_guest_function_profile_generation.fetch_add(1, std::memory_order_relaxed);
  g_guest_function_profile_enabled.store(true, std::memory_order_relaxed);
}

void ConfigureGuestIndirectCallCsv(const std::string& path) {
  CloseGuestIndirectCallCsv();

  const int32_t top_n = REXCVAR_GET(perf_guest_indirect_targets_top_n);
  if (path.empty() || top_n <= 0) {
    return;
  }

  g_guest_indirect_call_csv_path = BuildGuestIndirectCallCsvPath(path);
  g_guest_indirect_call_csv_file =
      rex::filesystem::OpenFile(rex::to_path(g_guest_indirect_call_csv_path), "w");
  if (!g_guest_indirect_call_csv_file) {
    REXLOG_WARN("perf: failed to open guest indirect target CSV log: {}",
                g_guest_indirect_call_csv_path);
    g_guest_indirect_call_csv_path.clear();
    return;
  }

  std::fputs("frame_index,elapsed_us,rank,source_guest_address,source_symbol,call_site,"
             "target_guest_address,calls,fast_path_hits,fallback_hits\n",
             g_guest_indirect_call_csv_file);
  g_guest_indirect_call_profile_enabled.store(true, std::memory_order_relaxed);
}

void SyncGuestFunctionCsv() {
  if (!g_csv_file || g_csv_path.empty()) {
    if (g_guest_function_csv_file ||
        g_guest_function_profile_enabled.load(std::memory_order_relaxed)) {
      CloseGuestFunctionCsv();
    }
    return;
  }

  const int32_t top_n = REXCVAR_GET(perf_guest_functions_top_n);
  if (top_n <= 0) {
    if (g_guest_function_csv_file ||
        g_guest_function_profile_enabled.load(std::memory_order_relaxed)) {
      CloseGuestFunctionCsv();
    } else {
      ResetGuestFunctionProfile();
    }
    return;
  }

  if (!g_guest_function_csv_file) {
    ConfigureGuestFunctionCsv(g_csv_path);
  }
}

void SyncGuestIndirectCallCsv() {
  if (!g_csv_file || g_csv_path.empty()) {
    if (g_guest_indirect_call_csv_file ||
        g_guest_indirect_call_profile_enabled.load(std::memory_order_relaxed)) {
      CloseGuestIndirectCallCsv();
    }
    return;
  }

  const int32_t top_n = REXCVAR_GET(perf_guest_indirect_targets_top_n);
  if (top_n <= 0) {
    if (g_guest_indirect_call_csv_file ||
        g_guest_indirect_call_profile_enabled.load(std::memory_order_relaxed)) {
      CloseGuestIndirectCallCsv();
    } else {
      ResetGuestIndirectCallProfile();
    }
    return;
  }

  if (!g_guest_indirect_call_csv_file) {
    ConfigureGuestIndirectCallCsv(g_csv_path);
  }
}

void WriteGuestFunctionCsvFrame(uint64_t frame_index, uint64_t elapsed_us) {
  if (!g_guest_function_csv_file) {
    return;
  }

  const int32_t top_n = REXCVAR_GET(perf_guest_functions_top_n);
  if (top_n <= 0) {
    CloseGuestFunctionCsv();
    return;
  }

  const int64_t min_exclusive = REXCVAR_GET(perf_guest_functions_min_exclusive_us);
  const auto entries = SnapshotGuestFunctionProfile(
      static_cast<size_t>(top_n), static_cast<uint64_t>(std::max<int64_t>(0, min_exclusive)));
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    std::fprintf(g_guest_function_csv_file,
                 "%llu,%llu,%llu,0x%08X,",
                 static_cast<unsigned long long>(frame_index),
                 static_cast<unsigned long long>(elapsed_us),
                 static_cast<unsigned long long>(i + 1), entry.address);
    WriteCsvCell(g_guest_function_csv_file, entry.symbol);
    std::fprintf(g_guest_function_csv_file,
                 ",%llu,%llu,%llu,%llu,%llu,%u\n",
                 static_cast<unsigned long long>(entry.calls),
                 static_cast<unsigned long long>(entry.inclusive_us),
                 static_cast<unsigned long long>(entry.exclusive_us),
                 static_cast<unsigned long long>(entry.blocking_wait_us),
                 static_cast<unsigned long long>(entry.active_exclusive_us),
                 entry.spin_hint_sites);
  }
}

void WriteGuestIndirectCallCsvFrame(uint64_t frame_index, uint64_t elapsed_us) {
  if (!g_guest_indirect_call_csv_file) {
    return;
  }

  const int32_t top_n = REXCVAR_GET(perf_guest_indirect_targets_top_n);
  if (top_n <= 0) {
    CloseGuestIndirectCallCsv();
    return;
  }

  const auto entries =
      SnapshotGuestIndirectCallTargetProfile(static_cast<size_t>(top_n));
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    std::fprintf(g_guest_indirect_call_csv_file,
                 "%llu,%llu,%llu,0x%08X,",
                 static_cast<unsigned long long>(frame_index),
                 static_cast<unsigned long long>(elapsed_us),
                 static_cast<unsigned long long>(i + 1), entry.source_address);
    WriteCsvCell(g_guest_indirect_call_csv_file, entry.source_symbol);
    std::fprintf(g_guest_indirect_call_csv_file,
                 ",0x%08X,0x%08X,%llu,%llu,%llu\n",
                 entry.call_site, entry.target_address,
                 static_cast<unsigned long long>(entry.calls),
                 static_cast<unsigned long long>(entry.fast_path_hits),
                 static_cast<unsigned long long>(entry.fallback_hits));
  }
}

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
  const uint64_t duration_us = DurationUsSince(start_tick);
  if (duration_us != 0) {
    IncrementCounter(id, static_cast<int64_t>(duration_us));
  }
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
  ResetGuestFunctionProfile();
  ResetGuestIndirectCallProfile();
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
  CloseGuestFunctionCsv();
  CloseGuestIndirectCallCsv();

  if (path.empty())
    return;

  g_csv_file = rex::filesystem::OpenFile(rex::to_path(path), "w");
  if (!g_csv_file) {
    REXLOG_WARN("perf: failed to open CSV log: {}", path);
    g_csv_path.clear();
    return;
  }
  g_csv_start_tick = rex::chrono::Clock::QueryHostTickCount();
  ConfigureGuestFunctionCsv(path);
  ConfigureGuestIndirectCallCsv(path);

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

  SyncGuestFunctionCsv();
  SyncGuestIndirectCallCsv();
  WriteGuestFunctionCsvFrame(g_csv_frame_count, elapsed_us);
  WriteGuestIndirectCallCsvFrame(g_csv_frame_count, elapsed_us);

  if (++g_csv_frame_count % 60 == 0) {
    std::fflush(g_csv_file);
    if (g_guest_function_csv_file) {
      std::fflush(g_guest_function_csv_file);
    }
    if (g_guest_indirect_call_csv_file) {
      std::fflush(g_guest_indirect_call_csv_file);
    }
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
  CloseGuestFunctionCsv();
  CloseGuestIndirectCallCsv();
}

ScopedCounterDuration::ScopedCounterDuration(CounterId id)
    : id_(id), start_tick_(rex::chrono::Clock::QueryHostTickCount()) {}

ScopedCounterDuration::~ScopedCounterDuration() {
  AddCounterDurationSince(id_, start_tick_);
}

void AddGuestFunctionDurationUs(uint32_t address, const char* symbol, uint64_t inclusive_us,
                                uint64_t exclusive_us, uint64_t blocking_wait_us,
                                uint32_t spin_hint_sites) {
  std::lock_guard lock(g_guest_function_profile_mutex);
  auto& entry = g_guest_function_profile[address];
  if (entry.symbol.empty() && symbol) {
    entry.symbol = symbol;
  }
  entry.spin_hint_sites = std::max(entry.spin_hint_sites, spin_hint_sites);
  ++entry.calls;
  entry.inclusive_us += inclusive_us;
  entry.exclusive_us += exclusive_us;
  entry.blocking_wait_us += std::min(blocking_wait_us, exclusive_us);
}

void AddGuestKernelWaitDurationUs(uint64_t duration_us) {
  if (duration_us == 0) {
    return;
  }

  IncrementCounter(CounterId::kGuestKernelWaitUs, static_cast<int64_t>(duration_us));
  if (!g_guest_function_stack.empty()) {
    g_guest_function_stack.back().blocking_wait_us += duration_us;
  }
}

void AddGuestIndirectCallTarget(uint32_t source_address, const char* source_symbol,
                                uint32_t call_site, uint32_t target_address,
                                bool fast_path_hit) {
  if (!g_guest_indirect_call_profile_enabled.load(std::memory_order_relaxed)) {
    return;
  }

  const GuestIndirectCallTargetKey key{
      .source_address = source_address,
      .call_site = call_site,
      .target_address = target_address,
  };

  std::lock_guard lock(g_guest_indirect_call_profile_mutex);
  auto& entry = g_guest_indirect_call_profile[key];
  if (entry.source_symbol.empty() && source_symbol) {
    entry.source_symbol = source_symbol;
  }
  ++entry.calls;
  if (fast_path_hit) {
    ++entry.fast_path_hits;
  } else {
    ++entry.fallback_hits;
  }
}

std::vector<GuestFunctionProfileEntry> SnapshotGuestFunctionProfile(size_t max_entries,
                                                                    uint64_t min_exclusive_us) {
  std::vector<GuestFunctionProfileEntry> entries;
  {
    std::lock_guard lock(g_guest_function_profile_mutex);
    entries.reserve(g_guest_function_profile.size());
    for (const auto& [address, totals] : g_guest_function_profile) {
      const uint64_t blocking_wait_us = std::min(totals.blocking_wait_us, totals.exclusive_us);
      const uint64_t active_exclusive_us = totals.exclusive_us - blocking_wait_us;
      if (active_exclusive_us < min_exclusive_us) {
        continue;
      }
      entries.push_back({
          .address = address,
          .symbol = totals.symbol,
          .calls = totals.calls,
          .inclusive_us = totals.inclusive_us,
          .exclusive_us = totals.exclusive_us,
          .blocking_wait_us = blocking_wait_us,
          .active_exclusive_us = active_exclusive_us,
          .spin_hint_sites = totals.spin_hint_sites,
      });
    }
    g_guest_function_profile.clear();
  }

  std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.active_exclusive_us != rhs.active_exclusive_us) {
      return lhs.active_exclusive_us > rhs.active_exclusive_us;
    }
    if (lhs.exclusive_us != rhs.exclusive_us) {
      return lhs.exclusive_us > rhs.exclusive_us;
    }
    if (lhs.inclusive_us != rhs.inclusive_us) {
      return lhs.inclusive_us > rhs.inclusive_us;
    }
    if (lhs.calls != rhs.calls) {
      return lhs.calls > rhs.calls;
    }
    return lhs.address < rhs.address;
  });
  if (entries.size() > max_entries) {
    entries.resize(max_entries);
  }
  return entries;
}

std::vector<GuestIndirectCallTargetProfileEntry> SnapshotGuestIndirectCallTargetProfile(
    size_t max_entries) {
  std::vector<GuestIndirectCallTargetProfileEntry> entries;
  {
    std::lock_guard lock(g_guest_indirect_call_profile_mutex);
    entries.reserve(g_guest_indirect_call_profile.size());
    for (const auto& [key, totals] : g_guest_indirect_call_profile) {
      entries.push_back({
          .source_address = key.source_address,
          .source_symbol = totals.source_symbol,
          .call_site = key.call_site,
          .target_address = key.target_address,
          .calls = totals.calls,
          .fast_path_hits = totals.fast_path_hits,
          .fallback_hits = totals.fallback_hits,
      });
    }
    g_guest_indirect_call_profile.clear();
  }

  std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.calls != rhs.calls) {
      return lhs.calls > rhs.calls;
    }
    if (lhs.fallback_hits != rhs.fallback_hits) {
      return lhs.fallback_hits > rhs.fallback_hits;
    }
    if (lhs.fast_path_hits != rhs.fast_path_hits) {
      return lhs.fast_path_hits > rhs.fast_path_hits;
    }
    if (lhs.source_address != rhs.source_address) {
      return lhs.source_address < rhs.source_address;
    }
    if (lhs.call_site != rhs.call_site) {
      return lhs.call_site < rhs.call_site;
    }
    return lhs.target_address < rhs.target_address;
  });
  if (entries.size() > max_entries) {
    entries.resize(max_entries);
  }
  return entries;
}

ScopedGuestFunctionProfile::ScopedGuestFunctionProfile(uint32_t address, const char* symbol,
                                                       uint32_t spin_hint_sites)
    : address_(address), symbol_(symbol), spin_hint_sites_(spin_hint_sites) {
  if (!g_guest_function_profile_enabled.load(std::memory_order_relaxed)) {
    return;
  }
  active_ = true;
  start_tick_ = rex::chrono::Clock::QueryHostTickCount();
  stack_index_ = g_guest_function_stack.size();
  stack_token_ = ++g_guest_function_stack_next_token;
  generation_ = g_guest_function_profile_generation.load(std::memory_order_relaxed);
  g_guest_function_stack.push_back({
      .address = address_,
      .symbol = symbol_,
      .start_tick = start_tick_,
      .token = stack_token_,
      .spin_hint_sites = spin_hint_sites_,
  });
}

ScopedGuestFunctionProfile::~ScopedGuestFunctionProfile() {
  if (!active_) {
    return;
  }

  if (stack_index_ >= g_guest_function_stack.size() ||
      g_guest_function_stack[stack_index_].token != stack_token_) {
    auto it = std::find_if(g_guest_function_stack.begin(), g_guest_function_stack.end(),
                           [token = stack_token_](const GuestFunctionStackEntry& entry) {
                             return entry.token == token;
                           });
    if (it == g_guest_function_stack.end()) {
      return;
    }
    stack_index_ = static_cast<size_t>(std::distance(g_guest_function_stack.begin(), it));
  }

  const GuestFunctionStackEntry entry = g_guest_function_stack[stack_index_];
  const uint64_t inclusive_us = DurationUsSince(entry.start_tick);
  const uint64_t exclusive_us = inclusive_us > entry.child_us ? inclusive_us - entry.child_us : 0;
  const uint64_t blocking_wait_us = std::min(entry.blocking_wait_us, exclusive_us);

  if (g_guest_function_stack.size() > stack_index_ + 1) {
    g_guest_function_stack.resize(stack_index_ + 1);
  }
  g_guest_function_stack.pop_back();

  if (!g_guest_function_stack.empty()) {
    g_guest_function_stack.back().child_us += inclusive_us;
  }

  if (generation_ == g_guest_function_profile_generation.load(std::memory_order_relaxed) &&
      g_guest_function_profile_enabled.load(std::memory_order_relaxed)) {
    AddGuestFunctionDurationUs(entry.address, entry.symbol, inclusive_us, exclusive_us,
                               blocking_wait_us, entry.spin_hint_sites);
  }
}

ScopedGuestKernelWaitProfile::ScopedGuestKernelWaitProfile()
    : start_tick_(rex::chrono::Clock::QueryHostTickCount()) {}

ScopedGuestKernelWaitProfile::~ScopedGuestKernelWaitProfile() {
  AddGuestKernelWaitDurationUs(DurationUsSince(start_tick_));
}

}  // namespace rex::perf
