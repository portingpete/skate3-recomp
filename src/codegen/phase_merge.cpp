/**
 * @file        codegen/phase_merge.cpp
 * @brief       Merge phase: resolve jumps and seal functions
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include "codegen_flags.h"
#include "ppc/disasm.h"

#include <rex/codegen/phases.h>

#include <rex/logging.h>

#include "codegen_logging.h"
#include "phase_helpers.h"
#include <rex/memory/utils.h>

#include <ppc.h>

using rex::memory::load_and_swap;
using rex::codegen::ppc::Disassemble;

namespace rex::codegen {

namespace {

//=============================================================================
// Merge to resolve jumps then seal functions
//=============================================================================
bool isPromotableBranchTarget(CodegenContext& ctx, uint32_t target) {
  if ((target & 3u) != 0) {
    return false;
  }
  auto* targetData = ctx.binary().translate(target);
  if (!targetData) {
    return false;
  }
  if (load_and_swap<uint32_t>(targetData) == 0) {
    return false;
  }
  if (ctx.binary().isExecutable(target) && !ctx.binary().isInImportExportRange(target)) {
    return true;
  }
  for (const auto& region : ctx.scan.codeRegions) {
    if (region.contains(target)) {
      return true;
    }
  }
  return false;
}

bool isPromotableAlternateEntry(const FunctionNode* owner, const FunctionNode* containing,
                                uint32_t target) {
  (void)containing;
  if (owner && (owner->containsAddress(target) || owner->isWithinBounds(target))) {
    return false;
  }
  return true;
}

bool isEntryRedirectAlternateEntry(const FunctionNode* owner, uint32_t site, uint32_t target) {
  if (!owner || target == owner->base() || target <= site || !owner->containsAddress(target)) {
    return false;
  }

  // Some titles use tiny entry shims that adapt argument registers and branch
  // into a second callable entry in the same PDATA range.
  constexpr uint32_t kMaxEntryRedirectBytes = 8;
  return site >= owner->base() && site <= owner->base() + kMaxEntryRedirectBytes;
}

bool hasCallEdgeAt(const FunctionNode* node, uint32_t site) {
  for (const auto& edge : node->calls()) {
    if (edge.site == site) {
      return true;
    }
  }
  for (const auto& edge : node->tailCalls()) {
    if (edge.site == site) {
      return true;
    }
  }
  return false;
}

uint32_t decodeConditionalBranchTarget(uint32_t instruction, uint32_t site) {
  int32_t displacement = static_cast<int32_t>(instruction & 0xFFFCu);
  if (displacement & 0x8000) {
    displacement |= ~0xFFFF;
  }
  return (instruction & 0x2u) ? static_cast<uint32_t>(displacement)
                             : site + static_cast<uint32_t>(displacement);
}

size_t promoteUnownedTailBranchTargets(CodegenContext& ctx) {
  struct Candidate {
    uint32_t site;
    uint32_t target;
    uint32_t owner;
    bool conditional;
  };

  auto& graph = ctx.graph;
  std::vector<Candidate> candidates;

  for (const auto* node : graph.getPendingFunctions()) {
    for (const auto& jump : node->unresolvedJumps()) {
      if (jump.isCall) {
        continue;
      }
      if (jump.isConditional && jump.target >= node->base()) {
        continue;
      }
      if (graph.isEntryPoint(jump.target) || graph.isImport(jump.target)) {
        continue;
      }
      if (!isPromotableAlternateEntry(node, graph.getFunctionContaining(jump.target),
                                      jump.target)) {
        continue;
      }
      if (!isPromotableBranchTarget(ctx, jump.target)) {
        continue;
      }
      candidates.push_back({jump.site, jump.target, node->base(), jump.isConditional});
    }
  }

  size_t promoted = 0;
  for (const auto& candidate : candidates) {
    if (graph.isEntryPoint(candidate.target) || graph.isImport(candidate.target)) {
      continue;
    }
    auto* owner = graph.getFunction(candidate.owner);
    if (!isPromotableAlternateEntry(owner, graph.getFunctionContaining(candidate.target),
                                    candidate.target)) {
      continue;
    }

    graph.addFunction(candidate.target, 4, FunctionAuthority::DISCOVERED, true);
    promoted++;
    REXCODEGEN_INFO(
        "Analyze: promoted unresolved {} target 0x{:08X} from 0x{:08X} "
        "(owner 0x{:08X})",
        candidate.conditional ? "conditional branch" : "tail branch", candidate.target,
        candidate.site, candidate.owner);
  }

  if (promoted > 0) {
    auto known = buildKnownFunctions(graph);
    size_t discovered = discoverPendingFunctions(ctx, known);
    REXCODEGEN_INFO("Analyze: discovered {} promoted branch target functions", discovered);
  }

  return promoted;
}

size_t promoteEdgeLessContainedBranchTargets(CodegenContext& ctx) {
  struct Candidate {
    uint32_t site;
    uint32_t target;
    uint32_t owner;
    bool conditional;
  };

  auto& graph = ctx.graph;
  auto& binary = ctx.binary();
  std::vector<Candidate> candidates;

  for (const auto& [ownerAddr, nodePtr] : graph.functions()) {
    const auto* node = nodePtr.get();
    if (!node || node->isImport() || node->blocks().empty()) {
      continue;
    }

    for (const auto& block : node->blocks()) {
      const uint8_t* data = binary.translate(block.base);
      if (!data) {
        continue;
      }

      for (uint32_t offset = 0; offset < block.size; offset += 4) {
        ppc_insn decoded;
        Disassemble(reinterpret_cast<const uint32_t*>(data + offset), 4, block.base + offset,
                    decoded);
        const uint32_t raw = load_and_swap<uint32_t>(data + offset);
        const bool isDirectConditionalBranch = (raw >> 26) == 16 && (raw & 1u) == 0;
        const bool isUnconditionalBranch = decoded.opcode && decoded.opcode->id == PPC_INST_B;
        const bool isConditionalBranch = isDirectConditionalBranch;
        if (!isUnconditionalBranch && !isConditionalBranch) {
          continue;
        }

        const uint32_t site = block.base + offset;
        if (hasCallEdgeAt(node, site)) {
          continue;
        }

        const uint32_t target = isConditionalBranch ? decodeConditionalBranchTarget(raw, site)
                                                    : decoded.operands[0];
        if (isConditionalBranch && target >= ownerAddr) {
          continue;
        }
        if (graph.isEntryPoint(target) || graph.isImport(target)) {
          continue;
        }

        const auto* containing = graph.getFunctionContaining(target);
        if (!isConditionalBranch && !isEntryRedirectAlternateEntry(node, site, target) &&
            !isPromotableAlternateEntry(node, containing, target)) {
          continue;
        }
        if (isConditionalBranch && !isPromotableAlternateEntry(node, containing, target)) {
          continue;
        }
        if (!isPromotableBranchTarget(ctx, target)) {
          continue;
        }

        candidates.push_back({site, target, ownerAddr, isConditionalBranch});
      }
    }
  }

  size_t promoted = 0;
  size_t edgesAdded = 0;
  for (const auto& candidate : candidates) {
    auto* owner = graph.getFunction(candidate.owner);
    if (!owner || owner->isSealed()) {
      continue;
    }
    if (hasCallEdgeAt(owner, candidate.site)) {
      continue;
    }
    if (graph.isImport(candidate.target)) {
      continue;
    }

    auto* target = graph.getFunction(candidate.target);
    if (!target) {
      const auto* containing = graph.getFunctionContaining(candidate.target);
      if (!isEntryRedirectAlternateEntry(owner, candidate.site, candidate.target) &&
          !isPromotableAlternateEntry(owner, containing, candidate.target)) {
        continue;
      }
      target = graph.addFunction(candidate.target, 4, FunctionAuthority::DISCOVERED, true);
      promoted++;
      REXCODEGEN_INFO(
          "Analyze: promoted contained {} target 0x{:08X} from 0x{:08X} "
          "(owner 0x{:08X})",
          candidate.conditional ? "conditional branch" : "tail branch", candidate.target,
          candidate.site, candidate.owner);
    }

    if (target) {
      graph.addTailCallToFunction(candidate.owner, candidate.site, CallTarget::function(target));
      edgesAdded++;
    }
  }

  if (promoted > 0) {
    auto known = buildKnownFunctions(graph);
    size_t discovered = discoverPendingFunctions(ctx, known);
    REXCODEGEN_INFO("Analyze: discovered {} contained branch target functions", discovered);
  }

  return promoted + edgesAdded;
}

void mergeAndSeal(CodegenContext& ctx) {
  REXCODEGEN_INFO("Analyze: resolving jumps and sealing functions...");

  auto& graph = ctx.graph;
  auto& binary = ctx.binary();

  graph.setMemoryReader([&binary](uint32_t addr) -> std::optional<uint32_t> {
    auto* section = binary.findSection(addr);
    if (!section || !section->data) {
      return std::nullopt;
    }
    uint32_t offset = addr - section->baseAddress;
    if (offset + 4 > section->size) {
      return std::nullopt;
    }
    return load_and_swap<uint32_t>(section->data + offset);
  });

  size_t iteration = 0;
  size_t totalResolved = 0;
  const size_t maxResolveIterations = REXCVAR_GET(max_resolve_iterations);

  while (iteration < maxResolveIterations) {
    iteration++;
    size_t changesThisIteration = 0;

    changesThisIteration += promoteUnownedTailBranchTargets(ctx);
    changesThisIteration += promoteEdgeLessContainedBranchTargets(ctx);

    std::vector<uint32_t> pendingAddrs;
    for (const auto* node : graph.getPendingFunctions()) {
      pendingAddrs.push_back(node->base());
    }

    for (uint32_t funcAddr : pendingAddrs) {
      size_t resolved = graph.tryResolveFunction(funcAddr);
      changesThisIteration += resolved;
      totalResolved += resolved;
    }

    if (changesThisIteration == 0)
      break;
  }

  size_t totalSealed = graph.sealAllReady();
  size_t stillPending = graph.pendingCount();

  REXCODEGEN_INFO("Analyze: {} iterations, resolved={}, sealed={}/{}", iteration, totalResolved,
                  totalSealed, graph.functionCount());

  if (stillPending > 0) {
    REXCODEGEN_WARN("Analyze: {} functions still PENDING with unresolved jumps", stillPending);
  }
}

}  // anonymous namespace

namespace phases {

VoidResult Merge(CodegenContext& ctx, ProgressReporter* reporter) {
  (void)reporter;
  mergeAndSeal(ctx);
  return Ok();
}

}  // namespace phases

}  // namespace rex::codegen
