#include <rex/perf/counter.h>

int main() {
#ifdef REXGLUE_ENABLE_PERF_COUNTERS
  (void)rex::perf::IsGuestFunctionProfileEnabled();
  PROFILE_GUEST_SPIN_HINT_EXECUTION();
#endif
  return 0;
}
