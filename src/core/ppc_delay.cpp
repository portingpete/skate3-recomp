/**
 * @file        core/ppc_delay.cpp
 * @brief       PPC delay execution hint policy cvars
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 * @license     BSD 3-Clause License
 */

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(ppc_delay_via_maybeyield, false, "PPC",
                    "Implement db16cyc delay hints via MaybeYield instead of host pause")
    .debug_only();
