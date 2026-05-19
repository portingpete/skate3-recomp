#pragma once

#include <cstdint>
#include <string>

namespace rex::graphics {

struct IssueDrawFailureInfo {
  const char* backend = "";
  const char* stage = "";
  uint32_t prim_type = 0;
  uint32_t index_count = 0;
  uint32_t source_select = 0;
  uint32_t major_mode = 0;
  bool explicit_major = false;
  uint32_t path_select = 0;
  uint32_t tess_mode = 0;
  uint32_t edram_mode = 0;
};

std::string FormatIssueDrawFailure(const IssueDrawFailureInfo& info);

}  // namespace rex::graphics
