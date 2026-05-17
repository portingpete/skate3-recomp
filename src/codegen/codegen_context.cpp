/**
 * @file        rexcodegen/codegen_context.cpp
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include "decoded_binary.h"

#include <cstddef>

#include <fmt/format.h>

#include <rex/codegen/codegen_context.h>
#include <rex/logging.h>

#include "codegen_logging.h"
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>

namespace rex::codegen {

namespace {

uint32_t ReadBe32(const SectionView& section, size_t offset) {
  const uint8_t* data = section.data + offset;
  return (uint32_t{data[0]} << 24) | (uint32_t{data[1]} << 16) | (uint32_t{data[2]} << 8) |
         uint32_t{data[3]};
}

bool HasWords(const SectionView& section, size_t offset, size_t count) {
  return section.data && offset + count * sizeof(uint32_t) <= section.size;
}

bool LooksLikePpcSetJmp(const SectionView& section, size_t offset) {
  if (!HasWords(section, offset, 6)) {
    return false;
  }

  const uint32_t w0 = ReadBe32(section, offset + 0);
  const uint32_t w1 = ReadBe32(section, offset + 4);
  const uint32_t w2 = ReadBe32(section, offset + 8);
  const uint32_t w3 = ReadBe32(section, offset + 12);
  const uint32_t w4 = ReadBe32(section, offset + 16);
  const uint32_t w5 = ReadBe32(section, offset + 20);

  return (w0 & 0xFFFF0000u) == 0x3C800000u &&  // lis r4,slot_hi
         (w1 & 0xFFFF0000u) == 0x80040000u &&  // lwz r0,slot_lo(r4)
         w2 == 0x2C000000u &&                  // cmpwi r0,0
         w3 == 0x7C0903A6u &&                  // mtctr r0
         w4 == 0x4C820420u &&                  // bnectr cr0
         w5 == 0x7C0802A6u;                    // mflr r0
}

bool LooksLikePpcLongJmp(const SectionView& section, size_t offset) {
  if (!HasWords(section, offset, 7)) {
    return false;
  }

  const uint32_t w0 = ReadBe32(section, offset + 0);
  const uint32_t w1 = ReadBe32(section, offset + 4);
  const uint32_t w2 = ReadBe32(section, offset + 8);
  const uint32_t w3 = ReadBe32(section, offset + 12);
  const uint32_t w4 = ReadBe32(section, offset + 16);
  const uint32_t w5 = ReadBe32(section, offset + 20);
  const uint32_t w6 = ReadBe32(section, offset + 24);

  return w0 == 0x7C0802A6u &&                  // mflr r0
         (w1 & 0xFFFF0000u) == 0x94210000u &&  // stwu r1,-frame(r1)
         w2 == 0x90010008u &&                  // stw r0,8(r1)
         w3 == 0x7C862378u &&                  // mr r6,r4
         w4 == 0x2C040000u &&                  // cmpwi r4,0
         w5 == 0x80030138u &&                  // lwz r0,312(r3)
         w6 == 0x2C800000u;                    // cmpwi cr1,r0,0
}

void DetectPpcSetJmpLongJmpHelpers(const BinaryView& binary, RecompilerConfig& config) {
  const bool needSetJmp = config.setJmpAddress == 0;
  const bool needLongJmp = config.longJmpAddress == 0;
  if (!needSetJmp && !needLongJmp) {
    return;
  }

  uint32_t setJmpCandidate = 0;
  uint32_t setJmpCount = 0;
  uint32_t longJmpCandidate = 0;
  uint32_t longJmpCount = 0;

  for (const auto& section : binary.sections()) {
    if (!section.executable || !section.data) {
      continue;
    }

    for (size_t offset = 0; offset + sizeof(uint32_t) <= section.size; offset += 4) {
      const uint32_t addr = section.baseAddress + static_cast<uint32_t>(offset);
      if (needSetJmp && LooksLikePpcSetJmp(section, offset)) {
        setJmpCandidate = addr;
        ++setJmpCount;
      }
      if (needLongJmp && LooksLikePpcLongJmp(section, offset)) {
        longJmpCandidate = addr;
        ++longJmpCount;
      }
    }
  }

  if (needSetJmp && setJmpCount == 1) {
    config.setJmpAddress = setJmpCandidate;
    REXCODEGEN_INFO("Auto-detected PPC setjmp helper at 0x{:08X}", setJmpCandidate);
  } else if (needSetJmp && setJmpCount > 1) {
    REXCODEGEN_WARN("Found {} PPC setjmp-like helpers; leaving setjmp_address unset",
                    setJmpCount);
  }

  if (needLongJmp && longJmpCount == 1) {
    config.longJmpAddress = longJmpCandidate;
    REXCODEGEN_INFO("Auto-detected PPC longjmp helper at 0x{:08X}", longJmpCandidate);
  } else if (needLongJmp && longJmpCount > 1) {
    REXCODEGEN_WARN("Found {} PPC longjmp-like helpers; leaving longjmp_address unset",
                    longJmpCount);
  }
}

}  // namespace

Result<CodegenContext> CodegenContext::Create(const std::filesystem::path& configPath,
                                              Runtime& runtime) {
  CodegenContext ctx;

  // Load configuration
  if (!ctx.config_.Load(configPath.string())) {
    return Err<CodegenContext>(ErrorCategory::Config,
                               fmt::format("Failed to load config: {}", configPath.string()));
  }
  ctx.configDir_ = configPath.parent_path();

  std::filesystem::path xexPath = ctx.configDir_ / ctx.config_.filePath;

  // Resolve path
  if (std::filesystem::exists(xexPath)) {
    xexPath = std::filesystem::canonical(xexPath);
  } else {
    return Err<CodegenContext>(ErrorCategory::NotFound,
                               fmt::format("XEX file not found: {}", xexPath.string()));
  }

  // Load XEX via Runtime
  auto xexFilename = xexPath.filename();
  auto vfsPath = "game:\\" + xexFilename.string();
  auto status = runtime.LoadXexImage(vfsPath);
  if (status != X_STATUS_SUCCESS) {
    return Err<CodegenContext>(
        ErrorCategory::Format,
        fmt::format("Failed to load XEX: {} (status {:#x})", xexPath.string(), status));
  }

  // Get module and create BinaryView
  auto user_module = runtime.kernel_state()->GetExecutableModule();
  if (!user_module) {
    return Err<CodegenContext>(ErrorCategory::Format,
                               "Failed to get executable module after loading");
  }

  auto* module = user_module->xex_module();
  if (!module) {
    return Err<CodegenContext>(ErrorCategory::Format, "Failed to get XexModule from UserModule");
  }

  ctx.binary_ = BinaryView::fromModule(*module);
  ctx.resolver_ = runtime.export_resolver();

  REXCODEGEN_TRACE("Loaded XEX: base=0x{:08X}, size=0x{:X}, entry=0x{:08X}",
                   ctx.binary_.baseAddress(), ctx.binary_.imageSize(), ctx.binary_.entryPoint());

  DetectPpcSetJmpLongJmpHelpers(ctx.binary_, ctx.config_);

  // Initialize AnalysisState from binary
  ctx.analysisState_.format = "xex";
  ctx.analysisState_.loadAddress = ctx.binary_.baseAddress();
  ctx.analysisState_.entryPoint = ctx.binary_.entryPoint();
  ctx.analysisState_.imageSize = ctx.binary_.imageSize();

  return ctx;
}

CodegenContext CodegenContext::Create(BinaryView binary, RecompilerConfig config) {
  CodegenContext ctx;
  ctx.binary_ = std::move(binary);
  ctx.config_ = std::move(config);
  DetectPpcSetJmpLongJmpHelpers(ctx.binary_, ctx.config_);
  return ctx;
}

CodegenContext::~CodegenContext() = default;
CodegenContext::CodegenContext(CodegenContext&&) = default;
CodegenContext& CodegenContext::operator=(CodegenContext&&) = default;

DecodedBinary& CodegenContext::decoded() {
  assert(decoded_ && "Call initDecoded() before accessing decoded()");
  return *decoded_;
}

const DecodedBinary& CodegenContext::decoded() const {
  assert(decoded_ && "Call initDecoded() before accessing decoded()");
  return *decoded_;
}

void CodegenContext::initDecoded() {
  decoded_ = std::make_unique<DecodedBinary>(binary_);
  decoded_->decode();
}

}  // namespace rex::codegen
