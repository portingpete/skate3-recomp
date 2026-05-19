/**
 * @file        rexcodegen/builders/control_flow.cpp
 * @brief       PPC control flow instruction code generation
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include "builder_context.h"
#include "helpers.h"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <rex/logging.h>

#include <string>
#include <vector>

#include "../codegen_logging.h"

namespace rex::codegen {

//=============================================================================
// Unconditional Branch
//=============================================================================

bool build_b(BuilderContext& ctx) {
  uint32_t target = ctx.insn.operands[0];

  auto kind = ctx.classify_branch_target(target, false);

  switch (kind) {
    case TargetKind::InternalLabel:
      // Target is within this function and not another function's entry point
      ctx.println("\tgoto loc_{:X};", target);
      break;

    case TargetKind::Function:
    case TargetKind::Import:
      // Tail call to another function or import
      ctx.emit_function_call(target);
      ctx.println("\treturn;");
      break;

    case TargetKind::Unknown:
      // Unknown target - fall back to range check
      if (target >= ctx.fn.base() && target < ctx.fn.end()) {
        ctx.println("\tgoto loc_{:X};", target);
      } else {
        REXCODEGEN_WARN("Unresolved b target 0x{:08X} from 0x{:08X}", target, ctx.base);
        ctx.emit_function_call(target);
        ctx.println("\treturn;");
      }
      break;
  }
  return true;
}

bool build_bl(BuilderContext& ctx) {
  uint32_t target = ctx.insn.operands[0];

  // Always set LR (unless skipLr)
  if (!ctx.config().skipLr)
    ctx.println("\tctx.lr = 0x{:X};", ctx.base + 4);

  auto kind = ctx.classify_branch_target(target, true);

  switch (kind) {
    case TargetKind::InternalLabel:
      // PIC code pattern - bl to get PC into LR, treat as local jump
      // LR is already set above, now jump to the target
      ctx.println("\tgoto loc_{:X};", target);
      break;

    case TargetKind::Function:
    case TargetKind::Import:
      ctx.emit_function_call(target);
      ctx.csrState = CSRState::Unknown;  // Call could change CSR state
      break;

    case TargetKind::Unknown:
      REXCODEGEN_ERROR("Unresolved bl target 0x{:08X} from 0x{:08X}", target, ctx.base);
      ctx.println("\t// ERROR: unresolved bl target 0x{:08X}", target);
      ctx.println("\tREX_FATAL(\"Unresolved call from 0x{:08X} to 0x{:08X}\");", ctx.base, target);
      break;
  }
  return true;
}

bool build_blr(BuilderContext& ctx) {
  ctx.println("\treturn;");
  return true;
}

bool build_blrl(BuilderContext& ctx) {
  // BLRL: save return address, then branch-and-link to current LR
  ctx.println("\t{{ auto old_lr = ctx.lr;");
  if (!ctx.config().skipLr)
    ctx.println("\tctx.lr = 0x{:X};", ctx.base + 4);
  ctx.emit_indirect_function_call("uint32_t(old_lr)");
  ctx.println("\t}}");
  ctx.csrState = CSRState::Unknown;
  return true;
}

namespace {

bool emit_bclr_from_bo_bi(BuilderContext& ctx, uint32_t bo, uint32_t bi, bool link) {
  if ((bo & 0x04) == 0) {
    ctx.println("\t--{}.u64;", ctx.ctr());
  }

  std::vector<std::string> conditions;
  if ((bo & 0x04) == 0) {
    conditions.push_back(fmt::format("{}.u32 {} 0", ctx.ctr(), (bo & 0x02) ? "==" : "!="));
  }
  if ((bo & 0x10) == 0) {
    auto cr_bit = fmt::format("{}.{}", ctx.cr(bi / 4), crBitName(bi));
    conditions.push_back((bo & 0x08) ? cr_bit : fmt::format("!{}", cr_bit));
  }

  if (link) {
    ctx.println("\t{{ auto old_lr = ctx.lr;");
    if (!ctx.config().skipLr)
      ctx.println("\tctx.lr = 0x{:X};", ctx.base + 4);

    if (conditions.empty()) {
      ctx.emit_indirect_function_call("uint32_t(old_lr)");
    } else {
      ctx.println("\tif ({}) {{", fmt::join(conditions, " && "));
      ctx.emit_indirect_function_call("uint32_t(old_lr)", "\t\t");
      ctx.println("\t}}");
    }

    ctx.println("\t}}");
    ctx.csrState = CSRState::Unknown;  // the call could change it
    return true;
  }

  if (conditions.empty()) {
    ctx.println("\treturn;");
    return true;
  }

  ctx.println("\tif ({}) {{", fmt::join(conditions, " && "));
  ctx.println("\t\treturn;");
  ctx.println("\t}}");
  return true;
}

}  // namespace

bool build_bclr(BuilderContext& ctx) {
  return emit_bclr_from_bo_bi(ctx, ctx.insn.operands[0], ctx.insn.operands[1], false);
}

bool build_bclrl(BuilderContext& ctx) {
  return emit_bclr_from_bo_bi(ctx, ctx.insn.operands[0], ctx.insn.operands[1], true);
}

//=============================================================================
// Count Register Branch
//=============================================================================

bool build_bctr(BuilderContext& ctx) {
  // Check active jump table (set by emitCpp before dispatch), then auto-detected
  const JumpTable* jt = ctx.activeJumpTable;

  if (!jt) {
    // Check auto-detected jump tables from function analysis
    for (const auto& autoJt : ctx.fn.jumpTables()) {
      if (autoJt.bctrAddress == ctx.base) {
        jt = &autoJt;
        break;
      }
    }
  }

  if (jt) {
    ctx.println("\tswitch ({}.u32) {{", ctx.r(jt->indexRegister));

    for (size_t i = 0; i < jt->targets.size(); i++) {
      ctx.println("\tcase {}:", i);
      auto label = jt->targets[i];

      // TODO(tomc): Figure out if this actually is triggered on real hardware and what would
      // happen?
      if (label == 0) {
        ctx.println("\t\t__builtin_trap(); // ERROR - detected jump to null value");
        continue;
      }

      auto kind = ctx.classify_branch_target(label, false);
      if (label != ctx.fn.base() && ctx.graph().isEntryPoint(label)) {
        kind = TargetKind::Function;
      }
      switch (kind) {
        case TargetKind::InternalLabel:
          ctx.println("\t\tgoto loc_{:X};", label);
          break;
        case TargetKind::Function:
        case TargetKind::Import:
          if (auto* targetFn = ctx.graph().getFunction(label)) {
            if (targetFn->isImport()) {
              ctx.emit_native_function_call(targetFn->base(), targetFn->name(), "\t\t");
            } else {
              ctx.println("\t\t{}(ctx, base);", targetFn->name());
            }
          } else {
            REXCODEGEN_ERROR(
                "Jump target 0x{:08X} classified as function but not in graph at bctr 0x{:08X}",
                label, ctx.base);
            ctx.println(
                "\t\tREX_FATAL(\"Jump target 0x{:08X} classified as function but not "
                "in graph at bctr 0x{:08X}\");",
                label, ctx.base);
          }
          ctx.println("\t\treturn;");
          break;
        default:
          REXCODEGEN_ERROR("Jump target 0x{:08X} unresolved at bctr 0x{:08X}", label, ctx.base);
          ctx.println("\t\tREX_FATAL(\"Jump target 0x{:08X} unresolved at bctr 0x{:08X}\");", label,
                      ctx.base);
          break;
      }
    }

    ctx.println("\tdefault:");
    ctx.emit_indirect_function_call(fmt::format("{}.u32", ctx.ctr()), "\t\t");
    ctx.println("\t\treturn;");
    ctx.println("\t}}");

    ctx.reset_switch_table();
  } else {
    // No switch table - assume tail call via CTR
    // NOTE(tomc): If this is actually an unresolved switch table, the code after
    // will be unreachable. This is caught during analysis by discover_blocks.
    // The validation phase will report missing switch tables.
    ctx.emit_indirect_function_call(fmt::format("{}.u32", ctx.ctr()));
    ctx.println("\treturn;");
  }
  return true;
}

bool build_bctrl(BuilderContext& ctx) {
  if (!ctx.config().skipLr)
    ctx.println("\tctx.lr = 0x{:X};", ctx.base + 4);
  ctx.emit_indirect_function_call(fmt::format("{}.u32", ctx.ctr()));
  ctx.csrState = CSRState::Unknown;  // the call could change it
  return true;
}

namespace {

bool emit_conditional_ctr(BuilderContext& ctx, bool invert, uint32_t crField, const char* bit,
                          bool link) {
  if (link && !ctx.config().skipLr)
    ctx.println("\tctx.lr = 0x{:X};", ctx.base + 4);
  ctx.println("\tif ({}{}.{}) {{", invert ? "!" : "", ctx.cr(crField), bit);
  ctx.emit_indirect_function_call(fmt::format("{}.u32", ctx.ctr()), "\t\t");
  if (!link)
    ctx.println("\t\treturn;");
  ctx.println("\t}}");
  if (link)
    ctx.csrState = CSRState::Unknown;  // the call could change it
  return true;
}

bool emit_conditional_ctr_cr(BuilderContext& ctx, bool invert, const char* bit, bool link) {
  return emit_conditional_ctr(ctx, invert, ctx.insn.operands[0], bit, link);
}

bool emit_conditional_ctr_bi(BuilderContext& ctx, bool invert, bool link) {
  const uint32_t bi = ctx.insn.operands[0];
  return emit_conditional_ctr(ctx, invert, bi / 4, crBitName(bi), link);
}

bool emit_ctr_from_bo_bi(BuilderContext& ctx, bool link) {
  const uint32_t bo = ctx.insn.operands[0];
  const uint32_t bi = ctx.insn.operands[1];

  // bcctr/bcctrl do not decrement or test CTR; BO only controls whether and
  // how the CR bit is tested before branching to CTR.
  if ((bo & 0x10) != 0) {
    if (link && !ctx.config().skipLr)
      ctx.println("\tctx.lr = 0x{:X};", ctx.base + 4);
    ctx.emit_indirect_function_call(fmt::format("{}.u32", ctx.ctr()));
    if (!link)
      ctx.println("\treturn;");
    if (link)
      ctx.csrState = CSRState::Unknown;  // the call could change it
    return true;
  }

  const bool expect_true = (bo & 0x08) != 0;
  return emit_conditional_ctr(ctx, !expect_true, bi / 4, crBitName(bi), link);
}

}  // namespace

bool build_bltctr(BuilderContext& ctx) {
  return emit_conditional_ctr_cr(ctx, false, "lt", false);
}

bool build_bltctrl(BuilderContext& ctx) {
  return emit_conditional_ctr_cr(ctx, false, "lt", true);
}

bool build_bgtctr(BuilderContext& ctx) {
  return emit_conditional_ctr_cr(ctx, false, "gt", false);
}

bool build_bgtctrl(BuilderContext& ctx) {
  return emit_conditional_ctr_cr(ctx, false, "gt", true);
}

bool build_beqctr(BuilderContext& ctx) {
  return emit_conditional_ctr_cr(ctx, false, "eq", false);
}

bool build_beqctrl(BuilderContext& ctx) {
  return emit_conditional_ctr_cr(ctx, false, "eq", true);
}

bool build_bsoctr(BuilderContext& ctx) {
  return emit_conditional_ctr_cr(ctx, false, "so", false);
}

bool build_bsoctrl(BuilderContext& ctx) {
  return emit_conditional_ctr_cr(ctx, false, "so", true);
}

bool build_bgectr(BuilderContext& ctx) {
  return emit_conditional_ctr_cr(ctx, true, "lt", false);
}

bool build_bgectrl(BuilderContext& ctx) {
  return emit_conditional_ctr_cr(ctx, true, "lt", true);
}

bool build_blectr(BuilderContext& ctx) {
  return emit_conditional_ctr_cr(ctx, true, "gt", false);
}

bool build_blectrl(BuilderContext& ctx) {
  return emit_conditional_ctr_cr(ctx, true, "gt", true);
}

bool build_bnectr(BuilderContext& ctx) {
  return emit_conditional_ctr_cr(ctx, true, "eq", false);
}

bool build_bnectrl(BuilderContext& ctx) {
  return emit_conditional_ctr_cr(ctx, true, "eq", true);
}

bool build_bnsctr(BuilderContext& ctx) {
  return emit_conditional_ctr_cr(ctx, true, "so", false);
}

bool build_bnsctrl(BuilderContext& ctx) {
  return emit_conditional_ctr_cr(ctx, true, "so", true);
}

bool build_btctr(BuilderContext& ctx) {
  return emit_conditional_ctr_bi(ctx, false, false);
}

bool build_btctrl(BuilderContext& ctx) {
  return emit_conditional_ctr_bi(ctx, false, true);
}

bool build_bfctr(BuilderContext& ctx) {
  return emit_conditional_ctr_bi(ctx, true, false);
}

bool build_bfctrl(BuilderContext& ctx) {
  return emit_conditional_ctr_bi(ctx, true, true);
}

bool build_bcctr(BuilderContext& ctx) {
  return emit_ctr_from_bo_bi(ctx, false);
}

bool build_bcctrl(BuilderContext& ctx) {
  return emit_ctr_from_bo_bi(ctx, true);
}

//=============================================================================
// Decrement Counter and Branch
//=============================================================================

bool build_bdz(BuilderContext& ctx) {
  ctx.println("\t--{}.u64;", ctx.ctr());
  emitBranchWithBoundsCheck(ctx, ctx.insn.operands[0], fmt::format("{}.u32 == 0", ctx.ctr()),
                            "bdz");
  return true;
}

bool build_bdzlr(BuilderContext& ctx) {
  ctx.println("\t--{}.u64;", ctx.ctr());
  ctx.println("\tif ({}.u32 == 0) return;", ctx.ctr());
  return true;
}

bool build_bdnzlr(BuilderContext& ctx) {
  ctx.println("\t--{}.u64;", ctx.ctr());
  ctx.println("\tif ({}.u32 != 0) return;", ctx.ctr());
  return true;
}

bool build_bdnzflr(BuilderContext& ctx) {
  return emit_bclr_from_bo_bi(ctx, 0, ctx.insn.operands[0], false);
}

bool build_bdnzflrl(BuilderContext& ctx) {
  return emit_bclr_from_bo_bi(ctx, 0, ctx.insn.operands[0], true);
}

bool build_bdnz(BuilderContext& ctx) {
  ctx.println("\t--{}.u64;", ctx.ctr());
  emitBranchWithBoundsCheck(ctx, ctx.insn.operands[0], fmt::format("{}.u32 != 0", ctx.ctr()),
                            "bdnz");
  return true;
}

bool build_bdnzf(BuilderContext& ctx) {
  auto bit = crBitName(ctx.insn.operands[0]);
  ctx.println("\t--{}.u64;", ctx.ctr());
  emitBranchWithBoundsCheck(
      ctx, ctx.insn.operands[1],
      fmt::format("{}.u32 != 0 && !{}.{}", ctx.ctr(), ctx.cr(ctx.insn.operands[0] / 4), bit),
      "bdnzf");
  return true;
}

bool build_bdnzt(BuilderContext& ctx) {
  auto bit = crBitName(ctx.insn.operands[0]);
  ctx.println("\t--{}.u64;", ctx.ctr());
  emitBranchWithBoundsCheck(
      ctx, ctx.insn.operands[1],
      fmt::format("{}.u32 != 0 && {}.{}", ctx.ctr(), ctx.cr(ctx.insn.operands[0] / 4), bit),
      "bdnzt");
  return true;
}

bool build_bdzf(BuilderContext& ctx) {
  auto bit = crBitName(ctx.insn.operands[0]);
  ctx.println("\t--{}.u64;", ctx.ctr());
  emitBranchWithBoundsCheck(
      ctx, ctx.insn.operands[1],
      fmt::format("{}.u32 == 0 && !{}.{}", ctx.ctr(), ctx.cr(ctx.insn.operands[0] / 4), bit),
      "bdzf");
  return true;
}

//=============================================================================
// Conditional Branch (eq)
//=============================================================================

bool build_beq(BuilderContext& ctx) {
  ctx.emit_conditional_branch(false, "eq");
  return true;
}

bool build_beqlr(BuilderContext& ctx) {
  ctx.println("\tif ({}.eq) return;", ctx.cr(ctx.insn.operands[0]));
  return true;
}

bool build_beqlrl(BuilderContext& ctx) {
  return emit_bclr_from_bo_bi(ctx, 0x0C, ctx.insn.operands[0] * 4 + 2, true);
}

bool build_bne(BuilderContext& ctx) {
  ctx.emit_conditional_branch(true, "eq");
  return true;
}

bool build_bnelr(BuilderContext& ctx) {
  ctx.println("\tif (!{}.eq) return;", ctx.cr(ctx.insn.operands[0]));
  return true;
}

bool build_bnelrl(BuilderContext& ctx) {
  return emit_bclr_from_bo_bi(ctx, 0x04, ctx.insn.operands[0] * 4 + 2, true);
}

//=============================================================================
// Conditional Branch (lt)
//=============================================================================

bool build_blt(BuilderContext& ctx) {
  ctx.emit_conditional_branch(false, "lt");
  return true;
}

bool build_bltlr(BuilderContext& ctx) {
  ctx.println("\tif ({}.lt) return;", ctx.cr(ctx.insn.operands[0]));
  return true;
}

bool build_bltlrl(BuilderContext& ctx) {
  return emit_bclr_from_bo_bi(ctx, 0x0C, ctx.insn.operands[0] * 4, true);
}

bool build_bge(BuilderContext& ctx) {
  ctx.emit_conditional_branch(true, "lt");
  return true;
}

bool build_bgelr(BuilderContext& ctx) {
  ctx.println("\tif (!{}.lt) return;", ctx.cr(ctx.insn.operands[0]));
  return true;
}

bool build_bgelrl(BuilderContext& ctx) {
  return emit_bclr_from_bo_bi(ctx, 0x04, ctx.insn.operands[0] * 4, true);
}

//=============================================================================
// Conditional Branch (gt)
//=============================================================================

bool build_bgt(BuilderContext& ctx) {
  ctx.emit_conditional_branch(false, "gt");
  return true;
}

bool build_bgtlr(BuilderContext& ctx) {
  ctx.println("\tif ({}.gt) return;", ctx.cr(ctx.insn.operands[0]));
  return true;
}

bool build_bgtlrl(BuilderContext& ctx) {
  return emit_bclr_from_bo_bi(ctx, 0x0C, ctx.insn.operands[0] * 4 + 1, true);
}

bool build_ble(BuilderContext& ctx) {
  ctx.emit_conditional_branch(true, "gt");
  return true;
}

bool build_blelr(BuilderContext& ctx) {
  ctx.println("\tif (!{}.gt) return;", ctx.cr(ctx.insn.operands[0]));
  return true;
}

bool build_blelrl(BuilderContext& ctx) {
  return emit_bclr_from_bo_bi(ctx, 0x04, ctx.insn.operands[0] * 4 + 1, true);
}

//=============================================================================
// Conditional Branch (so - summary overflow / unordered)
//=============================================================================

bool build_bso(BuilderContext& ctx) {
  ctx.emit_conditional_branch(false, "so");
  return true;
}

bool build_bsolr(BuilderContext& ctx) {
  ctx.println("\tif ({}.so) return;", ctx.cr(ctx.insn.operands[0]));
  return true;
}

bool build_bsolrl(BuilderContext& ctx) {
  return emit_bclr_from_bo_bi(ctx, 0x0C, ctx.insn.operands[0] * 4 + 3, true);
}

bool build_bns(BuilderContext& ctx) {
  ctx.emit_conditional_branch(true, "so");
  return true;
}

bool build_bnslr(BuilderContext& ctx) {
  ctx.println("\tif (!{}.so) return;", ctx.cr(ctx.insn.operands[0]));
  return true;
}

bool build_bnslrl(BuilderContext& ctx) {
  return emit_bclr_from_bo_bi(ctx, 0x04, ctx.insn.operands[0] * 4 + 3, true);
}

bool build_btlr(BuilderContext& ctx) {
  return emit_bclr_from_bo_bi(ctx, 0x0C, ctx.insn.operands[0], false);
}

bool build_btlrl(BuilderContext& ctx) {
  return emit_bclr_from_bo_bi(ctx, 0x0C, ctx.insn.operands[0], true);
}

bool build_bflr(BuilderContext& ctx) {
  return emit_bclr_from_bo_bi(ctx, 0x04, ctx.insn.operands[0], false);
}

bool build_bflrl(BuilderContext& ctx) {
  return emit_bclr_from_bo_bi(ctx, 0x04, ctx.insn.operands[0], true);
}

}  // namespace rex::codegen
