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
#include <numeric>
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
REXCVAR_DEFINE_INT32(perf_guest_direct_calls_top_n, 0, "Perf",
                     "Write the top N generated direct-call edges to a sidecar perf CSV");
REXCVAR_DEFINE_INT32(perf_guest_indirect_targets_top_n, 0, "Perf",
                     "Write the top N generated indirect-call targets to a sidecar perf CSV");

namespace rex::perf {

namespace detail {
std::atomic<bool> g_guest_function_profile_enabled{false};
std::atomic<bool> g_guest_direct_call_profile_enabled{false};
std::atomic<bool> g_guest_indirect_call_profile_enabled{false};
}  // namespace detail

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
    "audio_silence_frames",
    "audio_startup_silence_frames",
    "audio_underrun_frames",
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
    false,  // kAudioSilenceFrames
    false,  // kAudioStartupSilenceFrames
    false,  // kAudioUnderrunFrames
    true,   // kBufferQueueDepth  (live queued audio frames)
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
  uint32_t static_spin_hint_sites = 0;
  uint64_t dynamic_spin_hint_executions = 0;
};

struct GuestFunctionStackEntry {
  uint32_t address = 0;
  const char* symbol = nullptr;
  uint64_t start_tick = 0;
  uint64_t child_us = 0;
  uint64_t blocking_wait_us = 0;
  uint64_t token = 0;
  uint32_t static_spin_hint_sites = 0;
  uint64_t dynamic_spin_hint_executions = 0;
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
  std::string target_symbol;
  uint64_t calls = 0;
  uint64_t fast_path_hits = 0;
  uint64_t fallback_hits = 0;
};

struct GuestDirectCallTargetKey {
  uint32_t source_address = 0;
  uint32_t call_site = 0;
  uint32_t target_address = 0;

  bool operator==(const GuestDirectCallTargetKey& other) const {
    return source_address == other.source_address && call_site == other.call_site &&
           target_address == other.target_address;
  }
};

struct GuestDirectCallTargetKeyHash {
  size_t operator()(const GuestDirectCallTargetKey& key) const {
    size_t hash = std::hash<uint32_t>{}(key.source_address);
    hash ^= std::hash<uint32_t>{}(key.call_site) + 0x9E3779B9u + (hash << 6) + (hash >> 2);
    hash ^= std::hash<uint32_t>{}(key.target_address) + 0x9E3779B9u + (hash << 6) + (hash >> 2);
    return hash;
  }
};

struct GuestDirectCallProfileTotals {
  std::string source_symbol;
  std::string target_symbol;
  uint64_t calls = 0;
};

std::string FormatGuestFunctionSymbol(uint32_t address) {
  char buffer[13] = {};
  std::snprintf(buffer, sizeof(buffer), "sub_%08X", address);
  return buffer;
}

std::string FormatGuestCallSiteSymbol(uint32_t source_address, std::string_view source_symbol,
                                      uint32_t call_site) {
  if (source_address != 0 && call_site >= source_address) {
    std::string label = source_symbol.empty() ? FormatGuestFunctionSymbol(source_address)
                                              : std::string(source_symbol);
    const uint32_t offset = call_site - source_address;
    if (offset == 0) {
      return label;
    }

    char suffix[12] = {};
    std::snprintf(suffix, sizeof(suffix), "+0x%X", offset);
    label += suffix;
    return label;
  }

  if (call_site == 0) {
    return {};
  }
  return FormatGuestFunctionSymbol(call_site);
}

std::mutex g_guest_function_profile_mutex;
std::unordered_map<uint32_t, GuestFunctionProfileTotals> g_guest_function_profile;
std::unordered_map<uint32_t, GuestFunctionProfileTotals> g_guest_function_summary_profile;
std::atomic<uint64_t> g_guest_function_profile_generation{1};
std::FILE* g_guest_function_csv_file = nullptr;
std::string g_guest_function_csv_path;
std::string g_guest_function_summary_csv_path;
size_t g_guest_function_summary_top_n = 0;

std::mutex g_guest_direct_call_profile_mutex;
std::unordered_map<GuestDirectCallTargetKey, GuestDirectCallProfileTotals,
                   GuestDirectCallTargetKeyHash>
    g_guest_direct_call_profile;
std::unordered_map<GuestDirectCallTargetKey, GuestDirectCallProfileTotals,
                   GuestDirectCallTargetKeyHash>
    g_guest_direct_call_summary_profile;
std::FILE* g_guest_direct_call_csv_file = nullptr;
std::string g_guest_direct_call_csv_path;
std::string g_guest_direct_call_summary_csv_path;
size_t g_guest_direct_call_summary_top_n = 0;

std::mutex g_guest_indirect_call_profile_mutex;
std::unordered_map<GuestIndirectCallTargetKey, GuestIndirectCallTargetProfileTotals,
                   GuestIndirectCallTargetKeyHash>
    g_guest_indirect_call_profile;
std::unordered_map<GuestIndirectCallTargetKey, GuestIndirectCallTargetProfileTotals,
                   GuestIndirectCallTargetKeyHash>
    g_guest_indirect_call_summary_profile;
std::FILE* g_guest_indirect_call_csv_file = nullptr;
std::string g_guest_indirect_call_csv_path;
std::string g_guest_indirect_call_summary_csv_path;
size_t g_guest_indirect_call_summary_top_n = 0;

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
  g_guest_function_summary_profile.clear();
}

void ResetGuestDirectCallProfile() {
  std::lock_guard lock(g_guest_direct_call_profile_mutex);
  g_guest_direct_call_profile.clear();
  g_guest_direct_call_summary_profile.clear();
}

void ResetGuestIndirectCallProfile() {
  std::lock_guard lock(g_guest_indirect_call_profile_mutex);
  g_guest_indirect_call_profile.clear();
  g_guest_indirect_call_summary_profile.clear();
}

std::string BuildGuestFunctionCsvPath(const std::string& path) {
  return path + ".guest_functions.csv";
}

std::string BuildGuestFunctionSummaryCsvPath(const std::string& path) {
  return path + ".guest_functions.summary.csv";
}

std::string BuildGuestDirectCallCsvPath(const std::string& path) {
  return path + ".guest_direct_calls.csv";
}

std::string BuildGuestDirectCallSummaryCsvPath(const std::string& path) {
  return path + ".guest_direct_calls.summary.csv";
}

std::string BuildGuestIndirectCallCsvPath(const std::string& path) {
  return path + ".guest_indirect_targets.csv";
}

std::string BuildGuestIndirectCallSummaryCsvPath(const std::string& path) {
  return path + ".guest_indirect_targets.summary.csv";
}

std::string FormatPerCallMetric(uint64_t total, uint64_t calls) {
  if (calls == 0) {
    return "0.000";
  }

  char buffer[64] = {};
  std::snprintf(buffer, sizeof(buffer), "%.3f",
                static_cast<double>(total) / static_cast<double>(calls));
  return buffer;
}

std::string FormatPercentMetric(uint64_t part, uint64_t total) {
  if (total == 0) {
    return "0.000";
  }

  char buffer[64] = {};
  std::snprintf(buffer, sizeof(buffer), "%.3f",
                static_cast<double>(part) * 100.0 / static_cast<double>(total));
  return buffer;
}

size_t CurrentSummaryTopN(int32_t live_top_n, size_t configured_top_n) {
  if (live_top_n > 0) {
    return static_cast<size_t>(live_top_n);
  }
  return configured_top_n;
}

uint64_t CurrentGuestFunctionMinExclusiveUs() {
  return static_cast<uint64_t>(std::max<int64_t>(
      0, REXCVAR_GET(perf_guest_functions_min_exclusive_us)));
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

void AddGuestFunctionDurationUsLocked(std::unordered_map<uint32_t, GuestFunctionProfileTotals>& map,
                                      uint32_t address, const char* symbol, uint64_t inclusive_us,
                                      uint64_t exclusive_us, uint64_t blocking_wait_us,
                                      uint32_t static_spin_hint_sites,
                                      uint64_t dynamic_spin_hint_executions) {
  auto& entry = map[address];
  if (entry.symbol.empty() && symbol) {
    entry.symbol = symbol;
  }
  entry.static_spin_hint_sites = std::max(entry.static_spin_hint_sites, static_spin_hint_sites);
  entry.dynamic_spin_hint_executions += dynamic_spin_hint_executions;
  ++entry.calls;
  entry.inclusive_us += inclusive_us;
  entry.exclusive_us += exclusive_us;
  entry.blocking_wait_us += std::min(blocking_wait_us, exclusive_us);
}

void AddGuestIndirectCallTargetLocked(
    std::unordered_map<GuestIndirectCallTargetKey, GuestIndirectCallTargetProfileTotals,
                       GuestIndirectCallTargetKeyHash>& map,
    const GuestIndirectCallTargetKey& key, const char* source_symbol, const char* target_symbol,
    bool fast_path_hit) {
  auto& entry = map[key];
  if (entry.source_symbol.empty() && source_symbol) {
    entry.source_symbol = source_symbol;
  }
  if (entry.target_symbol.empty()) {
    if (target_symbol && target_symbol[0] != '\0') {
      entry.target_symbol = target_symbol;
    } else {
      entry.target_symbol = FormatGuestFunctionSymbol(key.target_address);
    }
  }
  ++entry.calls;
  if (fast_path_hit) {
    ++entry.fast_path_hits;
  } else {
    ++entry.fallback_hits;
  }
}

void AddGuestDirectCallTargetLocked(
    std::unordered_map<GuestDirectCallTargetKey, GuestDirectCallProfileTotals,
                       GuestDirectCallTargetKeyHash>& map,
    const GuestDirectCallTargetKey& key, const char* source_symbol, const char* target_symbol) {
  auto& entry = map[key];
  if (entry.source_symbol.empty() && source_symbol) {
    entry.source_symbol = source_symbol;
  }
  if (entry.target_symbol.empty() && target_symbol) {
    entry.target_symbol = target_symbol;
  }
  ++entry.calls;
}

std::vector<GuestFunctionProfileEntry> BuildGuestFunctionEntries(
    const std::unordered_map<uint32_t, GuestFunctionProfileTotals>& profile,
    uint64_t min_exclusive_us) {
  std::vector<GuestFunctionProfileEntry> entries;
  entries.reserve(profile.size());
  for (const auto& [address, totals] : profile) {
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
        .static_spin_hint_sites = totals.static_spin_hint_sites,
        .dynamic_spin_hint_executions = totals.dynamic_spin_hint_executions,
    });
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
  return entries;
}

std::vector<GuestIndirectCallTargetProfileEntry> BuildGuestIndirectCallTargetEntries(
    const std::unordered_map<GuestIndirectCallTargetKey, GuestIndirectCallTargetProfileTotals,
                             GuestIndirectCallTargetKeyHash>& profile) {
  std::vector<GuestIndirectCallTargetProfileEntry> entries;
  entries.reserve(profile.size());
  for (const auto& [key, totals] : profile) {
    entries.push_back({
        .source_address = key.source_address,
        .source_symbol = totals.source_symbol,
        .call_site = key.call_site,
        .target_address = key.target_address,
        .target_symbol = totals.target_symbol,
        .calls = totals.calls,
        .fast_path_hits = totals.fast_path_hits,
        .fallback_hits = totals.fallback_hits,
    });
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
  return entries;
}

std::vector<GuestDirectCallProfileEntry> BuildGuestDirectCallEntries(
    const std::unordered_map<GuestDirectCallTargetKey, GuestDirectCallProfileTotals,
                             GuestDirectCallTargetKeyHash>& profile) {
  std::vector<GuestDirectCallProfileEntry> entries;
  entries.reserve(profile.size());
  for (const auto& [key, totals] : profile) {
    entries.push_back({
        .source_address = key.source_address,
        .source_symbol = totals.source_symbol,
        .call_site = key.call_site,
        .target_address = key.target_address,
        .target_symbol = totals.target_symbol.empty() ? FormatGuestFunctionSymbol(key.target_address)
                                                      : totals.target_symbol,
        .calls = totals.calls,
    });
  }

  std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
    if (lhs.calls != rhs.calls) {
      return lhs.calls > rhs.calls;
    }
    if (lhs.source_address != rhs.source_address) {
      return lhs.source_address < rhs.source_address;
    }
    if (lhs.call_site != rhs.call_site) {
      return lhs.call_site < rhs.call_site;
    }
    return lhs.target_address < rhs.target_address;
  });
  return entries;
}

void WriteGuestFunctionSummaryCsv() {
  const size_t summary_top_n = CurrentSummaryTopN(
      REXCVAR_GET(perf_guest_functions_top_n), g_guest_function_summary_top_n);
  if (g_guest_function_summary_csv_path.empty() || summary_top_n == 0) {
    return;
  }

  auto* summary_file = rex::filesystem::OpenFile(rex::to_path(g_guest_function_summary_csv_path),
                                                 "w");
  if (!summary_file) {
    REXLOG_WARN("perf: failed to open guest-function summary CSV log: {}",
                g_guest_function_summary_csv_path);
    return;
  }

  std::vector<GuestFunctionProfileEntry> entries;
  {
    std::lock_guard lock(g_guest_function_profile_mutex);
    entries = BuildGuestFunctionEntries(g_guest_function_summary_profile,
                                        CurrentGuestFunctionMinExclusiveUs());
  }
  const uint64_t total_active_exclusive_us = std::accumulate(
      entries.begin(), entries.end(), uint64_t{0},
      [](uint64_t total, const GuestFunctionProfileEntry& entry) {
        return total + entry.active_exclusive_us;
      });
  if (entries.size() > summary_top_n) {
    entries.resize(summary_top_n);
  }

  std::fputs("rank,guest_address,symbol,calls,inclusive_us,exclusive_us,blocking_wait_us,"
             "active_exclusive_us,static_spin_hint_sites,dynamic_spin_hint_executions,"
             "active_exclusive_us_per_call,dynamic_spin_hint_executions_per_call,"
             "active_exclusive_percent\n",
             summary_file);
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    std::fprintf(summary_file, "%llu,0x%08X,",
                 static_cast<unsigned long long>(i + 1), entry.address);
    WriteCsvCell(summary_file, entry.symbol);
    const std::string active_us_per_call =
        FormatPerCallMetric(entry.active_exclusive_us, entry.calls);
    const std::string spin_hints_per_call =
        FormatPerCallMetric(entry.dynamic_spin_hint_executions, entry.calls);
    const std::string active_percent =
        FormatPercentMetric(entry.active_exclusive_us, total_active_exclusive_us);
    std::fprintf(summary_file, ",%llu,%llu,%llu,%llu,%llu,%u,%llu,%s,%s,%s\n",
                 static_cast<unsigned long long>(entry.calls),
                 static_cast<unsigned long long>(entry.inclusive_us),
                 static_cast<unsigned long long>(entry.exclusive_us),
                 static_cast<unsigned long long>(entry.blocking_wait_us),
                 static_cast<unsigned long long>(entry.active_exclusive_us),
                 entry.static_spin_hint_sites,
                 static_cast<unsigned long long>(entry.dynamic_spin_hint_executions),
                 active_us_per_call.c_str(),
                 spin_hints_per_call.c_str(),
                 active_percent.c_str());
  }

  std::fflush(summary_file);
  std::fclose(summary_file);
}

void WriteGuestDirectCallSummaryCsv() {
  const size_t summary_top_n = CurrentSummaryTopN(
      REXCVAR_GET(perf_guest_direct_calls_top_n), g_guest_direct_call_summary_top_n);
  if (g_guest_direct_call_summary_csv_path.empty() || summary_top_n == 0) {
    return;
  }

  auto* summary_file = rex::filesystem::OpenFile(
      rex::to_path(g_guest_direct_call_summary_csv_path), "w");
  if (!summary_file) {
    REXLOG_WARN("perf: failed to open guest direct call summary CSV log: {}",
                g_guest_direct_call_summary_csv_path);
    return;
  }

  std::vector<GuestDirectCallProfileEntry> entries;
  {
    std::lock_guard lock(g_guest_direct_call_profile_mutex);
    entries = BuildGuestDirectCallEntries(g_guest_direct_call_summary_profile);
  }
  if (entries.size() > summary_top_n) {
    entries.resize(summary_top_n);
  }

  std::fputs("rank,source_guest_address,source_symbol,call_site,call_site_symbol,"
             "target_guest_address,target_symbol,calls\n",
             summary_file);
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    std::fprintf(summary_file, "%llu,0x%08X,",
                 static_cast<unsigned long long>(i + 1), entry.source_address);
    WriteCsvCell(summary_file, entry.source_symbol);
    std::fprintf(summary_file, ",0x%08X,", entry.call_site);
    WriteCsvCell(summary_file,
                 FormatGuestCallSiteSymbol(entry.source_address, entry.source_symbol,
                                           entry.call_site));
    std::fprintf(summary_file, ",0x%08X,", entry.target_address);
    WriteCsvCell(summary_file, entry.target_symbol);
    std::fprintf(summary_file, ",%llu\n",
                 static_cast<unsigned long long>(entry.calls));
  }

  std::fflush(summary_file);
  std::fclose(summary_file);
}

void WriteGuestIndirectCallSummaryCsv() {
  const size_t summary_top_n = CurrentSummaryTopN(
      REXCVAR_GET(perf_guest_indirect_targets_top_n), g_guest_indirect_call_summary_top_n);
  if (g_guest_indirect_call_summary_csv_path.empty() || summary_top_n == 0) {
    return;
  }

  auto* summary_file = rex::filesystem::OpenFile(
      rex::to_path(g_guest_indirect_call_summary_csv_path), "w");
  if (!summary_file) {
    REXLOG_WARN("perf: failed to open guest indirect target summary CSV log: {}",
                g_guest_indirect_call_summary_csv_path);
    return;
  }

  std::vector<GuestIndirectCallTargetProfileEntry> entries;
  {
    std::lock_guard lock(g_guest_indirect_call_profile_mutex);
    entries = BuildGuestIndirectCallTargetEntries(g_guest_indirect_call_summary_profile);
  }
  if (entries.size() > summary_top_n) {
    entries.resize(summary_top_n);
  }

  std::fputs("rank,source_guest_address,source_symbol,call_site,call_site_symbol,"
             "target_guest_address,target_symbol,calls,fast_path_hits,fallback_hits,"
             "fast_path_hits_per_call,fallback_hits_per_call\n",
             summary_file);
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    std::fprintf(summary_file, "%llu,0x%08X,",
                 static_cast<unsigned long long>(i + 1), entry.source_address);
    WriteCsvCell(summary_file, entry.source_symbol);
    std::fprintf(summary_file, ",0x%08X,", entry.call_site);
    WriteCsvCell(summary_file,
                 FormatGuestCallSiteSymbol(entry.source_address, entry.source_symbol,
                                           entry.call_site));
    std::fprintf(summary_file, ",0x%08X,", entry.target_address);
    WriteCsvCell(summary_file, entry.target_symbol);
    const std::string fast_hits_per_call =
        FormatPerCallMetric(entry.fast_path_hits, entry.calls);
    const std::string fallback_hits_per_call =
        FormatPerCallMetric(entry.fallback_hits, entry.calls);
    std::fprintf(summary_file, ",%llu,%llu,%llu,%s,%s\n",
                 static_cast<unsigned long long>(entry.calls),
                 static_cast<unsigned long long>(entry.fast_path_hits),
                 static_cast<unsigned long long>(entry.fallback_hits),
                 fast_hits_per_call.c_str(),
                 fallback_hits_per_call.c_str());
  }

  std::fflush(summary_file);
  std::fclose(summary_file);
}

void CloseGuestFunctionCsv() {
  detail::g_guest_function_profile_enabled.store(false, std::memory_order_relaxed);
  g_guest_function_profile_generation.fetch_add(1, std::memory_order_relaxed);
  WriteGuestFunctionSummaryCsv();
  if (g_guest_function_csv_file) {
    std::fflush(g_guest_function_csv_file);
    std::fclose(g_guest_function_csv_file);
    g_guest_function_csv_file = nullptr;
  }
  g_guest_function_csv_path.clear();
  g_guest_function_summary_csv_path.clear();
  g_guest_function_summary_top_n = 0;
  ResetGuestFunctionProfile();
}

void CloseGuestDirectCallCsv() {
  detail::g_guest_direct_call_profile_enabled.store(false, std::memory_order_relaxed);
  WriteGuestDirectCallSummaryCsv();
  if (g_guest_direct_call_csv_file) {
    std::fflush(g_guest_direct_call_csv_file);
    std::fclose(g_guest_direct_call_csv_file);
    g_guest_direct_call_csv_file = nullptr;
  }
  g_guest_direct_call_csv_path.clear();
  g_guest_direct_call_summary_csv_path.clear();
  g_guest_direct_call_summary_top_n = 0;
  ResetGuestDirectCallProfile();
}

void CloseGuestIndirectCallCsv() {
  detail::g_guest_indirect_call_profile_enabled.store(false, std::memory_order_relaxed);
  WriteGuestIndirectCallSummaryCsv();
  if (g_guest_indirect_call_csv_file) {
    std::fflush(g_guest_indirect_call_csv_file);
    std::fclose(g_guest_indirect_call_csv_file);
    g_guest_indirect_call_csv_file = nullptr;
  }
  g_guest_indirect_call_csv_path.clear();
  g_guest_indirect_call_summary_csv_path.clear();
  g_guest_indirect_call_summary_top_n = 0;
  ResetGuestIndirectCallProfile();
}

void ConfigureGuestFunctionCsv(const std::string& path) {
  CloseGuestFunctionCsv();

  const int32_t top_n = REXCVAR_GET(perf_guest_functions_top_n);
  if (path.empty() || top_n <= 0) {
    return;
  }

  g_guest_function_csv_path = BuildGuestFunctionCsvPath(path);
  g_guest_function_summary_csv_path = BuildGuestFunctionSummaryCsvPath(path);
  g_guest_function_csv_file =
      rex::filesystem::OpenFile(rex::to_path(g_guest_function_csv_path), "w");
  if (!g_guest_function_csv_file) {
    REXLOG_WARN("perf: failed to open guest-function CSV log: {}", g_guest_function_csv_path);
    g_guest_function_csv_path.clear();
    g_guest_function_summary_csv_path.clear();
    return;
  }

  std::fputs("frame_index,elapsed_us,rank,guest_address,symbol,calls,inclusive_us,exclusive_us,"
             "blocking_wait_us,active_exclusive_us,static_spin_hint_sites,"
             "dynamic_spin_hint_executions,active_exclusive_us_per_call,"
             "dynamic_spin_hint_executions_per_call\n",
             g_guest_function_csv_file);
  g_guest_function_summary_top_n = static_cast<size_t>(top_n);
  g_guest_function_profile_generation.fetch_add(1, std::memory_order_relaxed);
  detail::g_guest_function_profile_enabled.store(true, std::memory_order_relaxed);
}

void ConfigureGuestDirectCallCsv(const std::string& path) {
  CloseGuestDirectCallCsv();

  const int32_t top_n = REXCVAR_GET(perf_guest_direct_calls_top_n);
  if (path.empty() || top_n <= 0) {
    return;
  }

  g_guest_direct_call_csv_path = BuildGuestDirectCallCsvPath(path);
  g_guest_direct_call_summary_csv_path = BuildGuestDirectCallSummaryCsvPath(path);
  g_guest_direct_call_csv_file =
      rex::filesystem::OpenFile(rex::to_path(g_guest_direct_call_csv_path), "w");
  if (!g_guest_direct_call_csv_file) {
    REXLOG_WARN("perf: failed to open guest direct call CSV log: {}",
                g_guest_direct_call_csv_path);
    g_guest_direct_call_csv_path.clear();
    g_guest_direct_call_summary_csv_path.clear();
    return;
  }

  std::fputs("frame_index,elapsed_us,rank,source_guest_address,source_symbol,call_site,"
             "call_site_symbol,target_guest_address,target_symbol,calls\n",
             g_guest_direct_call_csv_file);
  g_guest_direct_call_summary_top_n = static_cast<size_t>(top_n);
  detail::g_guest_direct_call_profile_enabled.store(true, std::memory_order_relaxed);
}

void ConfigureGuestIndirectCallCsv(const std::string& path) {
  CloseGuestIndirectCallCsv();

  const int32_t top_n = REXCVAR_GET(perf_guest_indirect_targets_top_n);
  if (path.empty() || top_n <= 0) {
    return;
  }

  g_guest_indirect_call_csv_path = BuildGuestIndirectCallCsvPath(path);
  g_guest_indirect_call_summary_csv_path = BuildGuestIndirectCallSummaryCsvPath(path);
  g_guest_indirect_call_csv_file =
      rex::filesystem::OpenFile(rex::to_path(g_guest_indirect_call_csv_path), "w");
  if (!g_guest_indirect_call_csv_file) {
    REXLOG_WARN("perf: failed to open guest indirect target CSV log: {}",
                g_guest_indirect_call_csv_path);
    g_guest_indirect_call_csv_path.clear();
    g_guest_indirect_call_summary_csv_path.clear();
    return;
  }

  std::fputs("frame_index,elapsed_us,rank,source_guest_address,source_symbol,call_site,"
             "call_site_symbol,target_guest_address,target_symbol,calls,fast_path_hits,"
             "fallback_hits\n",
             g_guest_indirect_call_csv_file);
  g_guest_indirect_call_summary_top_n = static_cast<size_t>(top_n);
  detail::g_guest_indirect_call_profile_enabled.store(true, std::memory_order_relaxed);
}

void SyncGuestFunctionCsv() {
  if (!g_csv_file || g_csv_path.empty()) {
    if (g_guest_function_csv_file ||
        detail::g_guest_function_profile_enabled.load(std::memory_order_relaxed)) {
      CloseGuestFunctionCsv();
    }
    return;
  }

  const int32_t top_n = REXCVAR_GET(perf_guest_functions_top_n);
  if (top_n <= 0) {
    if (g_guest_function_csv_file ||
        detail::g_guest_function_profile_enabled.load(std::memory_order_relaxed)) {
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

void SyncGuestDirectCallCsv() {
  if (!g_csv_file || g_csv_path.empty()) {
    if (g_guest_direct_call_csv_file ||
        detail::g_guest_direct_call_profile_enabled.load(std::memory_order_relaxed)) {
      CloseGuestDirectCallCsv();
    }
    return;
  }

  const int32_t top_n = REXCVAR_GET(perf_guest_direct_calls_top_n);
  if (top_n <= 0) {
    if (g_guest_direct_call_csv_file ||
        detail::g_guest_direct_call_profile_enabled.load(std::memory_order_relaxed)) {
      CloseGuestDirectCallCsv();
    } else {
      ResetGuestDirectCallProfile();
    }
    return;
  }

  if (!g_guest_direct_call_csv_file) {
    ConfigureGuestDirectCallCsv(g_csv_path);
  }
}

void SyncGuestIndirectCallCsv() {
  if (!g_csv_file || g_csv_path.empty()) {
    if (g_guest_indirect_call_csv_file ||
        detail::g_guest_indirect_call_profile_enabled.load(std::memory_order_relaxed)) {
      CloseGuestIndirectCallCsv();
    }
    return;
  }

  const int32_t top_n = REXCVAR_GET(perf_guest_indirect_targets_top_n);
  if (top_n <= 0) {
    if (g_guest_indirect_call_csv_file ||
        detail::g_guest_indirect_call_profile_enabled.load(std::memory_order_relaxed)) {
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
    const std::string active_us_per_call =
        FormatPerCallMetric(entry.active_exclusive_us, entry.calls);
    const std::string spin_hints_per_call =
        FormatPerCallMetric(entry.dynamic_spin_hint_executions, entry.calls);
    std::fprintf(g_guest_function_csv_file,
                 ",%llu,%llu,%llu,%llu,%llu,%u,%llu,%s,%s\n",
                 static_cast<unsigned long long>(entry.calls),
                 static_cast<unsigned long long>(entry.inclusive_us),
                 static_cast<unsigned long long>(entry.exclusive_us),
                 static_cast<unsigned long long>(entry.blocking_wait_us),
                 static_cast<unsigned long long>(entry.active_exclusive_us),
                 entry.static_spin_hint_sites,
                 static_cast<unsigned long long>(entry.dynamic_spin_hint_executions),
                 active_us_per_call.c_str(), spin_hints_per_call.c_str());
  }
}

void WriteGuestDirectCallCsvFrame(uint64_t frame_index, uint64_t elapsed_us) {
  if (!g_guest_direct_call_csv_file) {
    return;
  }

  const int32_t top_n = REXCVAR_GET(perf_guest_direct_calls_top_n);
  if (top_n <= 0) {
    CloseGuestDirectCallCsv();
    return;
  }

  const auto entries = SnapshotGuestDirectCallProfile(static_cast<size_t>(top_n));
  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& entry = entries[i];
    std::fprintf(g_guest_direct_call_csv_file,
                 "%llu,%llu,%llu,0x%08X,",
                 static_cast<unsigned long long>(frame_index),
                 static_cast<unsigned long long>(elapsed_us),
                 static_cast<unsigned long long>(i + 1), entry.source_address);
    WriteCsvCell(g_guest_direct_call_csv_file, entry.source_symbol);
    std::fprintf(g_guest_direct_call_csv_file, ",0x%08X,", entry.call_site);
    WriteCsvCell(g_guest_direct_call_csv_file,
                 FormatGuestCallSiteSymbol(entry.source_address, entry.source_symbol,
                                           entry.call_site));
    std::fprintf(g_guest_direct_call_csv_file, ",0x%08X,", entry.target_address);
    WriteCsvCell(g_guest_direct_call_csv_file, entry.target_symbol);
    std::fprintf(g_guest_direct_call_csv_file, ",%llu\n",
                 static_cast<unsigned long long>(entry.calls));
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
                 ",0x%08X,",
                 entry.call_site);
    WriteCsvCell(g_guest_indirect_call_csv_file,
                 FormatGuestCallSiteSymbol(entry.source_address, entry.source_symbol,
                                           entry.call_site));
    std::fprintf(g_guest_indirect_call_csv_file,
                 ",0x%08X,",
                 entry.target_address);
    WriteCsvCell(g_guest_indirect_call_csv_file, entry.target_symbol);
    std::fprintf(g_guest_indirect_call_csv_file,
                 ",%llu,%llu,%llu\n",
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
  ResetGuestDirectCallProfile();
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
  CloseGuestDirectCallCsv();
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
  ConfigureGuestDirectCallCsv(path);
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
  SyncGuestDirectCallCsv();
  SyncGuestIndirectCallCsv();
  WriteGuestFunctionCsvFrame(g_csv_frame_count, elapsed_us);
  WriteGuestDirectCallCsvFrame(g_csv_frame_count, elapsed_us);
  WriteGuestIndirectCallCsvFrame(g_csv_frame_count, elapsed_us);

  if (++g_csv_frame_count % 60 == 0) {
    std::fflush(g_csv_file);
    if (g_guest_function_csv_file) {
      std::fflush(g_guest_function_csv_file);
      WriteGuestFunctionSummaryCsv();
    }
    if (g_guest_direct_call_csv_file) {
      std::fflush(g_guest_direct_call_csv_file);
      WriteGuestDirectCallSummaryCsv();
    }
    if (g_guest_indirect_call_csv_file) {
      std::fflush(g_guest_indirect_call_csv_file);
      WriteGuestIndirectCallSummaryCsv();
    }
  }
}

void FlushCsv() {
  if (g_csv_file) {
    std::fflush(g_csv_file);
    std::fclose(g_csv_file);
    g_csv_file = nullptr;
  }
  CloseGuestFunctionCsv();
  CloseGuestDirectCallCsv();
  CloseGuestIndirectCallCsv();
  g_csv_path.clear();
  g_csv_frame_count = 0;
  g_csv_start_tick = 0;
}

ScopedCounterDuration::ScopedCounterDuration(CounterId id)
    : id_(id), start_tick_(rex::chrono::Clock::QueryHostTickCount()) {}

ScopedCounterDuration::~ScopedCounterDuration() {
  AddCounterDurationSince(id_, start_tick_);
}

void AddGuestFunctionDurationUs(uint32_t address, const char* symbol, uint64_t inclusive_us,
                                uint64_t exclusive_us, uint64_t blocking_wait_us,
                                uint32_t static_spin_hint_sites,
                                uint64_t dynamic_spin_hint_executions) {
  std::lock_guard lock(g_guest_function_profile_mutex);
  AddGuestFunctionDurationUsLocked(g_guest_function_profile, address, symbol, inclusive_us,
                                   exclusive_us, blocking_wait_us, static_spin_hint_sites,
                                   dynamic_spin_hint_executions);
  AddGuestFunctionDurationUsLocked(g_guest_function_summary_profile, address, symbol,
                                   inclusive_us, exclusive_us, blocking_wait_us,
                                   static_spin_hint_sites, dynamic_spin_hint_executions);
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

void AddGuestSpinHintExecution() {
  AddGuestSpinHintExecutions(1);
}

void AddGuestSpinHintExecutions(uint64_t count) {
  if (count == 0) {
    return;
  }

  if (!detail::g_guest_function_profile_enabled.load(std::memory_order_relaxed) ||
      g_guest_function_stack.empty()) {
    return;
  }
  g_guest_function_stack.back().dynamic_spin_hint_executions += count;
}

void AddGuestDirectCallTarget(uint32_t source_address, const char* source_symbol,
                              uint32_t call_site, uint32_t target_address,
                              const char* target_symbol) {
  if (!detail::g_guest_direct_call_profile_enabled.load(std::memory_order_relaxed)) {
    return;
  }

  const GuestDirectCallTargetKey key{
      .source_address = source_address,
      .call_site = call_site,
      .target_address = target_address,
  };

  std::lock_guard lock(g_guest_direct_call_profile_mutex);
  AddGuestDirectCallTargetLocked(g_guest_direct_call_profile, key, source_symbol,
                                 target_symbol);
  AddGuestDirectCallTargetLocked(g_guest_direct_call_summary_profile, key, source_symbol,
                                 target_symbol);
}

void AddGuestIndirectCallTarget(uint32_t source_address, const char* source_symbol,
                                uint32_t call_site, uint32_t target_address,
                                bool fast_path_hit) {
  if (!detail::g_guest_indirect_call_profile_enabled.load(std::memory_order_relaxed)) {
    return;
  }

  const std::string target_symbol = FormatGuestFunctionSymbol(target_address);
  AddGuestIndirectCallTarget(source_address, source_symbol, call_site, target_address,
                             target_symbol.c_str(), fast_path_hit);
}

void AddGuestIndirectCallTarget(uint32_t source_address, const char* source_symbol,
                                uint32_t call_site, uint32_t target_address,
                                const char* target_symbol, bool fast_path_hit) {
  if (!detail::g_guest_indirect_call_profile_enabled.load(std::memory_order_relaxed)) {
    return;
  }

  const GuestIndirectCallTargetKey key{
      .source_address = source_address,
      .call_site = call_site,
      .target_address = target_address,
  };

  std::lock_guard lock(g_guest_indirect_call_profile_mutex);
  AddGuestIndirectCallTargetLocked(g_guest_indirect_call_profile, key, source_symbol,
                                   target_symbol, fast_path_hit);
  AddGuestIndirectCallTargetLocked(g_guest_indirect_call_summary_profile, key, source_symbol,
                                   target_symbol, fast_path_hit);
}

std::vector<GuestFunctionProfileEntry> SnapshotGuestFunctionProfile(size_t max_entries,
                                                                    uint64_t min_exclusive_us) {
  std::vector<GuestFunctionProfileEntry> entries;
  {
    std::lock_guard lock(g_guest_function_profile_mutex);
    entries = BuildGuestFunctionEntries(g_guest_function_profile, min_exclusive_us);
    g_guest_function_profile.clear();
  }

  if (entries.size() > max_entries) {
    entries.resize(max_entries);
  }
  return entries;
}

std::vector<GuestDirectCallProfileEntry> SnapshotGuestDirectCallProfile(size_t max_entries) {
  std::vector<GuestDirectCallProfileEntry> entries;
  {
    std::lock_guard lock(g_guest_direct_call_profile_mutex);
    entries = BuildGuestDirectCallEntries(g_guest_direct_call_profile);
    g_guest_direct_call_profile.clear();
  }

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
    entries = BuildGuestIndirectCallTargetEntries(g_guest_indirect_call_profile);
    g_guest_indirect_call_profile.clear();
  }

  if (entries.size() > max_entries) {
    entries.resize(max_entries);
  }
  return entries;
}

ScopedGuestFunctionProfile::ScopedGuestFunctionProfile(uint32_t address, const char* symbol,
                                                       uint32_t static_spin_hint_sites)
    : address_(address), symbol_(symbol), static_spin_hint_sites_(static_spin_hint_sites) {
  if (!detail::g_guest_function_profile_enabled.load(std::memory_order_relaxed)) {
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
      .static_spin_hint_sites = static_spin_hint_sites_,
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
      detail::g_guest_function_profile_enabled.load(std::memory_order_relaxed)) {
    AddGuestFunctionDurationUs(entry.address, entry.symbol, inclusive_us, exclusive_us,
                               blocking_wait_us, entry.static_spin_hint_sites,
                               entry.dynamic_spin_hint_executions);
  }
}

ScopedGuestKernelWaitProfile::ScopedGuestKernelWaitProfile()
    : start_tick_(rex::chrono::Clock::QueryHostTickCount()) {}

ScopedGuestKernelWaitProfile::~ScopedGuestKernelWaitProfile() {
  AddGuestKernelWaitDurationUs(DurationUsSince(start_tick_));
}

}  // namespace rex::perf
