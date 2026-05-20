#include <rex/perf/counter.h>
#include <rex/ppc/context.h>
#include <rex/cvar.h>

#include <filesystem>

int main() {
  if (rex::runtime::IndirectDispatchGeneration() == 0) {
    return 10;
  }

#ifdef REXGLUE_ENABLE_PERF_COUNTERS
  const auto csv_path =
      std::filesystem::temp_directory_path() / "rex_perf_sdk_import_smoke.csv";
  const auto indirect_csv_path =
      std::filesystem::path(csv_path.string() + ".guest_indirect_targets.csv");
  const auto indirect_summary_path =
      std::filesystem::path(csv_path.string() + ".guest_indirect_targets.summary.csv");

  rex::perf::FlushCsv();
  rex::perf::Init();
  std::error_code ec;
  std::filesystem::remove(csv_path, ec);
  std::filesystem::remove(indirect_csv_path, ec);
  std::filesystem::remove(indirect_summary_path, ec);

  (void)rex::perf::IsGuestFunctionProfileEnabled();
  (void)rex::perf::IsGuestIndirectCallProfileEnabled();
  if (rex::perf::IsGuestIndirectCallProfileEnabled()) {
    return 1;
  }
  PROFILE_GUEST_SPIN_HINT_EXECUTION();
  PROFILE_GUEST_SPIN_HINT_EXECUTIONS(4);
  rex::ppc_delay_execution_hints(4);
  PROFILE_GUEST_INDIRECT_CALL_TARGET(0x82000000, "sub_82000000", 0x82000010, 0x82001000, true);
  PROFILE_GUEST_INDIRECT_CALL_TARGET_WITH_SYMBOL(0x82000000, "sub_82000000", 0x82000014,
                                                 0x82002000, "sub_82002000", false);

  if (!rex::cvar::SetFlagByName("perf_log_csv", csv_path.string()) ||
      !rex::cvar::SetFlagByName("perf_guest_indirect_targets_top_n", "1")) {
    return 2;
  }
  rex::perf::ConfigureCsvLogPathFromCvar();
  if (!rex::perf::IsGuestIndirectCallProfileEnabled()) {
    return 3;
  }

  PROFILE_GUEST_INDIRECT_CALL_TARGET(0x82000000, "sub_82000000", 0x82000010, 0x82001000, true);
  PROFILE_GUEST_INDIRECT_CALL_TARGET_WITH_SYMBOL(0x82000000, "sub_82000000", 0x82000014,
                                                 0x82002000, "sub_82002000", false);

  if (!rex::cvar::SetFlagByName("perf_guest_indirect_targets_top_n", "0")) {
    return 4;
  }
  rex::perf::ConfigureCsvLogPathFromCvar();
  if (rex::perf::IsGuestIndirectCallProfileEnabled()) {
    return 5;
  }

  rex::perf::FlushCsv();
  std::filesystem::remove(csv_path, ec);
  std::filesystem::remove(indirect_csv_path, ec);
  std::filesystem::remove(indirect_summary_path, ec);
#endif
  return 0;
}
