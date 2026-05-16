/**
 * @file        codegen/phase_discover.cpp
 * @brief       Discover phase: iterative function block discovery
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include "codegen_flags.h"
#include "decoded_binary.h"
#include <rex/codegen/function_scanner.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <unordered_set>

#include <rex/codegen/phases.h>
#include "phase_helpers.h"

#include <rex/codegen/vtable_scanner.h>
#include <rex/logging.h>

#include "codegen_logging.h"
#include <rex/memory/utils.h>

#include <ppc.h>

using rex::codegen::ppc::Opcode;
using rex::memory::load_and_swap;

namespace rex::codegen {

namespace {

//=============================================================================
// Discover Phase: iterative function block discovery
//=============================================================================

void discoverFunction(CodegenContext& ctx, uint32_t funcAddr,
                      const std::unordered_set<uint32_t>& knownFunctions) {
  auto& graph = ctx.graph;
  auto& binary = ctx.binary();
  auto& decoded = ctx.decoded();

  auto* node = graph.getFunction(funcAddr);
  if (!node)
    return;

  // Skip if already discovered
  if (!node->canDiscover()) {
    REXCODEGEN_TRACE("Analyze: function 0x{:08X} already discovered, skipping", funcAddr);
    return;
  }

  // Imports don't need block discovery
  if (node->isImport()) {
    node->discoverAsImport();
    return;
  }

  REXCODEGEN_TRACE("Analyze: discovering function 0x{:08X} ({})", funcAddr, node->name());

  // Lookup pdataSize for exception handler boundary
  uint32_t pdataSize = 0;

  // For CONFIG functions: use only the explicitly declared size (if any)
  // If no size specified (size=0), let discovery find natural boundaries via region
  // Don't inherit PDATA sizes for CONFIG functions - they're user hints for entry points
  if (node->authority() == FunctionAuthority::CONFIG) {
    pdataSize = node->size();  // 0 if not specified, which is correct
    REXCODEGEN_TRACE("Analyze: 0x{:08X} is CONFIG, using declared size={}", funcAddr, pdataSize);
  } else {
    // For non-CONFIG functions, use PDATA size if available
    auto pdataIt = ctx.scan.pdataSizes.find(funcAddr);
    if (pdataIt != ctx.scan.pdataSizes.end()) {
      pdataSize = pdataIt->second;
      REXCODEGEN_TRACE("Analyze: 0x{:08X} using PDATA size={}", funcAddr, pdataSize);
    }
  }

  // Find the code region containing this function
  const CodeRegion* region = nullptr;
  for (const auto& r : ctx.scan.codeRegions) {
    if (r.contains(funcAddr)) {
      region = &r;
      break;
    }
  }
  if (!region) {
    REXCODEGEN_WARN("Analyze: function 0x{:08X} not in any code region", funcAddr);
    return;
  }

  // Pass pdataSize so forward branches within function extent are correctly identified
  auto result = discoverBlocks(decoded, funcAddr, *region, knownFunctions, pdataSize);

  if (result.blocks.empty()) {
    REXCODEGEN_WARN("Analyze: no blocks found for function 0x{:08X}", funcAddr);
    return;
  }

  // snooper the function with the discovered blocks and instructions
  node->discover(std::move(result.blocks), std::move(result.instructions),
                 std::move(result.labels));

  // Add jump tables (targets become labels in the function)
  for (const auto& jt : result.jumpTables) {
    graph.addJumpTableToFunction(funcAddr, jt);
  }

  // Register external call targets as new functions (bl only, not b)
  for (uint32_t target : result.externalCalls) {
    if (!graph.isEntryPoint(target) && !graph.isImport(target)) {
      if (binary.isInImportExportRange(target)) {
        continue;
      }
      graph.addFunction(target, 4, FunctionAuthority::DISCOVERED, true);
    }
  }

  // Add unresolved branches for later resolution
  for (const auto& branch : result.unresolvedBranches) {
    graph.addUnresolvedJumpToFunction(funcAddr, branch.site, branch.target, branch.isCall,
                                      branch.isConditional);
  }

  // Scan exception handler regions for branches not in discovered blocks
  if (pdataSize > 0) {
    std::unordered_set<uint32_t> discoveredAddrs;
    for (const auto& block : result.blocks) {
      for (uint32_t addr = block.base; addr < block.base + block.size; addr += 4) {
        discoveredAddrs.insert(addr);
      }
    }

    uint32_t pdataEnd = funcAddr + pdataSize;
    const uint8_t* funcData = binary.translate(funcAddr);
    if (funcData) {
      for (uint32_t offset = 0; offset < pdataSize; offset += 4) {
        uint32_t site = funcAddr + offset;

        // Skip if already discovered by normal control flow
        if (discoveredAddrs.count(site))
          continue;

        // Skip if marked invalid
        auto invalidIt = ctx.analysisState().invalidInstructions.find(site);
        if (invalidIt != ctx.analysisState().invalidInstructions.end()) {
          continue;
        }

        uint32_t insn = load_and_swap<uint32_t>(funcData + offset);
        uint32_t opcode = PPC_OP(insn);

        if (opcode != PPC_OP_B && opcode != PPC_OP_BC)
          continue;

        uint32_t target = 0;
        bool isCall = PPC_BL(insn);
        bool isAbsolute = PPC_BA(insn);

        if (opcode == PPC_OP_B) {
          int32_t branchOffset = PPC_BI(insn);
          target = isAbsolute ? static_cast<uint32_t>(branchOffset) : site + branchOffset;
        } else {
          int32_t branchOffset = PPC_BD(insn);
          target = isAbsolute ? static_cast<uint32_t>(branchOffset) : site + branchOffset;
        }

        // Skip internal jumps within pdata region
        if (!isCall && target >= funcAddr && target < pdataEnd) {
          continue;
        }

        graph.addUnresolvedJumpToFunction(funcAddr, site, target, isCall, false);

        // Register call targets as new functions
        if (isCall && !graph.isEntryPoint(target) && !graph.isImport(target)) {
          if (binary.isInImportExportRange(target)) {
            continue;
          }
          graph.addFunction(target, 4, FunctionAuthority::DISCOVERED, true);
        }
      }
    }
  }
}

bool IsRawCodePointerTableSection(const SectionView& section) {
  if (section.executable) {
    return false;
  }

  return section.name == ".rdata" || section.name == ".data" || section.name == "rdata" ||
         section.name == "data";
}

bool IsInCodeRegion(const std::vector<CodeRegion>& codeRegions, uint32_t addr) {
  auto it = std::upper_bound(
      codeRegions.begin(), codeRegions.end(), addr,
      [](uint32_t value, const CodeRegion& region) { return value < region.start; });
  if (it == codeRegions.begin()) {
    return false;
  }

  --it;
  return it->contains(addr);
}

bool IsPotentialRawCodePointerTarget(const BinaryView& binary, const DecodedBinary& decoded,
                                     const std::vector<CodeRegion>& codeRegions,
                                     uint32_t target) {
  if ((target & 0x3) != 0 || binary.isInImportExportRange(target) ||
      !binary.isExecutable(target)) {
    return false;
  }

  if (!IsInCodeRegion(codeRegions, target)) {
    return false;
  }

  const auto* targetInsn = decoded.get(target);
  return targetInsn && !isInvalid(*targetInsn);
}

bool IsDiscoveredBlockInterior(const FunctionGraph& graph, uint32_t target) {
  if (graph.isEntryPoint(target)) {
    return false;
  }

  const auto* owner = graph.getFunctionContaining(target);
  if (!owner) {
    return false;
  }

  for (const auto& block : owner->blocks()) {
    if (block.contains(target)) {
      return true;
    }
  }

  return false;
}

bool TryDecodeReturnedCodePointer(const DecodedInsn& lisInsn, const DecodedInsn& combineInsn,
                                  uint32_t& target) {
  constexpr uint32_t kReturnRegister = 3;
  const uint32_t sourceReg = lisInsn.D.RT;
  const uint32_t high = static_cast<uint32_t>(static_cast<int16_t>(lisInsn.D.d)) << 16;

  if (combineInsn.D.RT != kReturnRegister || combineInsn.D.RA != sourceReg) {
    return false;
  }

  if (combineInsn.opcode == Opcode::addi) {
    target = high + static_cast<uint32_t>(static_cast<int16_t>(combineInsn.D.d));
    return true;
  }

  if (combineInsn.opcode == Opcode::ori) {
    target = high | static_cast<uint32_t>(static_cast<uint16_t>(combineInsn.D.d));
    return true;
  }

  return false;
}

size_t scanRawCodePointerTables(CodegenContext& ctx) {
  if (!ctx.hasDecoded()) {
    return 0;
  }

  constexpr size_t kMinTableSlots = 3;

  auto& graph = ctx.graph;
  auto& binary = ctx.binary();
  auto& decoded = ctx.decoded();
  std::vector<CodeRegion> codeRegions = ctx.scan.codeRegions;
  std::sort(codeRegions.begin(), codeRegions.end(),
            [](const CodeRegion& lhs, const CodeRegion& rhs) { return lhs.start < rhs.start; });

  std::vector<uint32_t> targets;
  std::unordered_set<uint32_t> queuedTargets;
  size_t tableCount = 0;

  for (const auto& section : binary.sections()) {
    if (!IsRawCodePointerTableSection(section)) {
      continue;
    }

    for (uint32_t offset = 0; offset + sizeof(uint32_t) <= section.size;) {
      const uint32_t runStartOffset = offset;
      const uint32_t runStartAddr = section.baseAddress + offset;
      std::vector<uint32_t> runTargets;

      while (offset + sizeof(uint32_t) <= section.size) {
        const uint32_t target = load_and_swap<uint32_t>(section.data + offset);
        if (!IsPotentialRawCodePointerTarget(binary, decoded, codeRegions, target)) {
          break;
        }

        runTargets.push_back(target);
        offset += sizeof(uint32_t);
      }

      if (runTargets.size() >= kMinTableSlots) {
        ++tableCount;

        const bool hasCallableAnchor =
            std::any_of(runTargets.begin(), runTargets.end(), [&](uint32_t candidate) {
              return graph.isEntryPoint(candidate) || graph.isImport(candidate) ||
                     !IsDiscoveredBlockInterior(graph, candidate);
            });

        for (uint32_t target : runTargets) {
          if (graph.isEntryPoint(target) || graph.isImport(target)) {
            continue;
          }

          const bool isInteriorEntry = IsDiscoveredBlockInterior(graph, target);
          if (isInteriorEntry && !hasCallableAnchor) {
            continue;
          }

          if (!queuedTargets.insert(target).second) {
            continue;
          }

          targets.push_back(target);
          REXCODEGEN_TRACE("Analyze: raw code pointer table 0x{:08X} references 0x{:08X}",
                           runStartAddr, target);
        }
      }

      offset = runTargets.empty() ? runStartOffset + sizeof(uint32_t) : offset;
    }
  }

  for (uint32_t target : targets) {
    graph.addFunction(target, 4, FunctionAuthority::VTABLE, true);
  }

  REXCODEGEN_INFO("Analyze: raw code pointer table scan found {} tables, {} new functions",
                  tableCount, targets.size());
  return targets.size();
}

}  // anonymous namespace

size_t scanReturnedCodePointerThunks(CodegenContext& ctx) {
  if (!ctx.hasDecoded()) {
    return 0;
  }

  auto& graph = ctx.graph;
  auto& binary = ctx.binary();
  auto& decoded = ctx.decoded();

  std::vector<uint32_t> targets;
  std::unordered_set<uint32_t> queuedTargets;

  for (const auto& [funcAddr, node] : graph.functions()) {
    if (!node->isDiscovered() || node->isImport()) {
      continue;
    }

    const auto instructions = node->instructions();
    if (instructions.size() < 3) {
      continue;
    }

    for (size_t i = 0; i + 2 < instructions.size(); i++) {
      const auto* lisInsn = instructions[i];
      const auto* combineInsn = instructions[i + 1];
      const auto* returnInsn = instructions[i + 2];
      if (!lisInsn || !combineInsn || !returnInsn) {
        continue;
      }

      if (!isLis(*lisInsn) || !isReturn(*returnInsn)) {
        continue;
      }

      uint32_t target = 0;
      if (!TryDecodeReturnedCodePointer(*lisInsn, *combineInsn, target)) {
        continue;
      }

      if ((target & 0x3) != 0 || graph.isEntryPoint(target) ||
          binary.isInImportExportRange(target)) {
        continue;
      }

      const auto* targetInsn = decoded.get(target);
      if (!targetInsn || isInvalid(*targetInsn) || !decoded.regionContaining(target)) {
        continue;
      }

      if (!queuedTargets.insert(target).second) {
        continue;
      }

      targets.push_back(target);
      REXCODEGEN_TRACE(
          "Analyze: returned code pointer 0x{:08X} from function 0x{:08X} at 0x{:08X}",
          target, funcAddr, combineInsn->address);
    }
  }

  for (uint32_t target : targets) {
    graph.addFunction(target, 4, FunctionAuthority::DISCOVERED, true);
  }

  REXCODEGEN_INFO("Analyze: returned code pointer scan found {} new functions", targets.size());
  return targets.size();
}

namespace {

void discoverAllFunctions(CodegenContext& ctx) {
  REXCODEGEN_TRACE("Analyze: starting iterative discovery...");

  auto& graph = ctx.graph;
  auto& binary = ctx.binary();

  // Iterative discovery
  size_t iteration = 0;
  size_t lastFunctionCount = 0;
  const size_t maxIterations = REXCVAR_GET(max_discovery_iterations);

  while (iteration < maxIterations) {
    iteration++;

    size_t currentFunctionCount = graph.functionCount();
    if (currentFunctionCount == lastFunctionCount && iteration > 1) {
      REXCODEGEN_DEBUG("Analyze: fixed point at iteration {} ({} functions)", iteration,
                       currentFunctionCount);
      break;
    }

    lastFunctionCount = currentFunctionCount;

    auto knownFunctions = buildKnownFunctions(graph);
    if (discoverPendingFunctions(ctx, knownFunctions) == 0) {
      break;
    }
  }

  REXCODEGEN_TRACE("Analyze: {} functions after call graph expansion", graph.functionCount());

  // VTable scanning
  {
    VTableScanner vtScanner(binary);
    auto vtables = vtScanner.scan();

    size_t newFunctions = 0;

    for (const auto& vt : vtables) {
      for (size_t i = 0; i < vt.slots.size(); i++) {
        uint32_t funcAddr = vt.slots[i];

        if (graph.isEntryPoint(funcAddr))
          continue;
        if (binary.isInImportExportRange(funcAddr))
          continue;

        graph.addFunction(funcAddr, 4, FunctionAuthority::VTABLE, true);
        newFunctions++;
      }
    }

    REXCODEGEN_TRACE("Analyze: VTable scan found {} vtables, {} new functions", vtables.size(),
                     newFunctions);

    // Continue discovery for vtable functions
    if (newFunctions > 0) {
      size_t vtableIteration = 0;
      const size_t maxVtableIterations = REXCVAR_GET(max_vtable_iterations);

      while (vtableIteration < maxVtableIterations) {
        vtableIteration++;

        auto knownFunctions = buildKnownFunctions(graph);
        if (discoverPendingFunctions(ctx, knownFunctions) == 0)
          break;

        if (graph.functionCount() == lastFunctionCount)
          break;
        lastFunctionCount = graph.functionCount();
      }
    }
  }

  REXCODEGEN_TRACE("Analyze: {} total functions after vtable scan", graph.functionCount());

  const size_t rawPointerFunctions = scanRawCodePointerTables(ctx);
  if (rawPointerFunctions > 0) {
    size_t rawPointerIteration = 0;
    while (rawPointerIteration < maxIterations) {
      rawPointerIteration++;

      auto knownFunctions = buildKnownFunctions(graph);
      if (discoverPendingFunctions(ctx, knownFunctions) == 0) {
        break;
      }

      if (graph.functionCount() == lastFunctionCount) {
        break;
      }
      lastFunctionCount = graph.functionCount();
    }
  }

  REXCODEGEN_TRACE("Analyze: {} total functions after raw code pointer table scan",
                   graph.functionCount());

  const size_t returnedPointerFunctions = scanReturnedCodePointerThunks(ctx);
  if (returnedPointerFunctions > 0) {
    size_t returnedPointerIteration = 0;
    while (returnedPointerIteration < maxIterations) {
      returnedPointerIteration++;

      auto knownFunctions = buildKnownFunctions(graph);
      if (discoverPendingFunctions(ctx, knownFunctions) == 0) {
        break;
      }

      if (graph.functionCount() == lastFunctionCount) {
        break;
      }
      lastFunctionCount = graph.functionCount();
    }
  }

  REXCODEGEN_TRACE("Analyze: {} total functions after returned pointer scan",
                   graph.functionCount());
}

//=============================================================================
// Function Pointer Scan: find lis/addi pairs loading code addresses
// TODO(tomc): THIS IS WIP AND PROB A BAD IDEA LOL LETS SEE
//=============================================================================
[[maybe_unused]] void functionPointerScan(CodegenContext& ctx) {
  if (!ctx.hasDecoded()) {
    REXCODEGEN_WARN("functionPointerScan: DecodedBinary not initialized, skipping");
    return;
  }

  auto& graph = ctx.graph;
  auto& decoded = ctx.decoded();
  const auto& codeRegions = decoded.codeRegions();

  if (codeRegions.empty()) {
    REXCODEGEN_WARN("functionPointerScan: no code regions, skipping");
    return;
  }

  // Build set of existing functions to avoid duplicates
  std::unordered_set<uint32_t> existingFunctions;
  for (const auto& [addr, node] : graph.functions()) {
    existingFunctions.insert(addr);
  }

  // Track lis values: register -> (high_value, lis_address)
  // We scan linearly and track the most recent lis for each register
  // PPC has exactly 32 GPRs, so a fixed-size array is more efficient than a map
  std::array<std::pair<uint32_t, uint32_t>, 32> lisValues{};
  std::bitset<32> lisValid;

  size_t foundCount = 0;

  for (const auto& region : codeRegions) {
    lisValid.reset();  // Reset tracking at region boundaries

    for (uint32_t addr = region.start; addr < region.end; addr += 4) {
      auto* insn = decoded.get(addr);
      if (!insn)
        continue;

      // Track lis rD, IMM
      if (isLis(*insn)) {
        uint8_t rd = static_cast<uint8_t>(insn->D.RT);
        uint32_t hi = static_cast<uint32_t>(static_cast<int16_t>(insn->D.d)) << 16;
        lisValues[rd] = {hi, addr};
        lisValid.set(rd);
        continue;
      }

      // Check for addi rD, rA, IMM where rA was set by lis
      if (insn->opcode == rex::codegen::ppc::Opcode::addi) {
        uint8_t ra = static_cast<uint8_t>(insn->D.RA);
        if (ra == 0)
          continue;  // li pseudo-op, not addi

        if (!lisValid.test(ra))
          continue;

        uint32_t hi = lisValues[ra].first;
        int16_t lo = static_cast<int16_t>(insn->D.d);
        uint32_t fullAddr = hi + lo;  // Sign-extended add

        // PPC instructions are 4-byte aligned
        if (fullAddr & 0x3)
          continue;

        // Check if this address is in a code region
        const CodeRegion* targetRegion = decoded.regionContaining(fullAddr);
        if (!targetRegion)
          continue;

        // Skip if already a known function
        if (existingFunctions.contains(fullAddr))
          continue;

        // Skip if it's an internal address (within same function's likely range)
        // Heuristic: if target is very close to current address, probably internal label
        int32_t distance = static_cast<int32_t>(fullAddr) - static_cast<int32_t>(addr);
        if (distance > -0x1000 && distance < 0x1000) {
          // Could be local label, skip for now
          continue;
        }

        // Register as function with DISCOVERED authority and hasXrefs=true
        graph.addFunction(fullAddr, 4, FunctionAuthority::DISCOVERED, true);
        existingFunctions.insert(fullAddr);
        foundCount++;

        REXCODEGEN_TRACE("functionPointerScan: found 0x{:08X} via lis/addi at 0x{:08X}", fullAddr,
                         addr);
      }

      // Also check ori rD, rA, IMM (alternative to addi for unsigned)
      if (insn->opcode == rex::codegen::ppc::Opcode::ori) {
        uint8_t ra = static_cast<uint8_t>(insn->D.RA);
        if (!lisValid.test(ra))
          continue;

        uint32_t hi = lisValues[ra].first;
        uint16_t lo = static_cast<uint16_t>(insn->D.d);
        uint32_t fullAddr = hi | lo;  // Unsigned OR

        // PPC instructions are 4-byte aligned
        if (fullAddr & 0x3)
          continue;

        const CodeRegion* targetRegion = decoded.regionContaining(fullAddr);
        if (!targetRegion)
          continue;

        if (existingFunctions.contains(fullAddr))
          continue;

        int32_t distance = static_cast<int32_t>(fullAddr) - static_cast<int32_t>(addr);
        if (distance > -0x1000 && distance < 0x1000)
          continue;

        graph.addFunction(fullAddr, 4, FunctionAuthority::DISCOVERED, true);
        existingFunctions.insert(fullAddr);
        foundCount++;

        REXCODEGEN_TRACE("functionPointerScan: found 0x{:08X} via lis/ori at 0x{:08X}", fullAddr,
                         addr);
      }

      // Clear lis tracking if register is overwritten by other instruction
      // (Simplified: we clear on any write to the register)
      // This is conservative - could miss some patterns but avoids false positives
    }
  }

  REXCODEGEN_TRACE("functionPointerScan: found {} new function pointer targets", foundCount);
}

}  // anonymous namespace

/// Discover blocks for all pending functions (shared helper, declared in phase_helpers.h).
size_t discoverPendingFunctions(CodegenContext& ctx,
                                const std::unordered_set<uint32_t>& knownFunctions) {
  std::vector<uint32_t> pending;
  for (const auto& [addr, node] : ctx.graph.functions()) {
    if (node->canDiscover()) {
      pending.push_back(addr);
    }
  }
  for (uint32_t funcAddr : pending) {
    discoverFunction(ctx, funcAddr, knownFunctions);
  }
  return pending.size();
}

namespace phases {

VoidResult Discover(CodegenContext& ctx, ProgressReporter* reporter) {
  (void)reporter;
  discoverAllFunctions(ctx);
  return Ok();
}

}  // namespace phases

}  // namespace rex::codegen
