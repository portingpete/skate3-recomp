#include <rex/graphics/draw_diagnostics.h>

#include <fmt/format.h>

namespace rex::graphics {

std::string FormatIssueDrawFailure(const IssueDrawFailureInfo& info) {
  return fmt::format(
      "{} IssueDraw failed at {} "
      "(prim_type={}, index_count={}, source_select={}, major_mode={}, explicit_major={}, "
      "path_select={}, tess_mode={}, edram_mode={})",
      info.backend, info.stage, info.prim_type, info.index_count, info.source_select,
      info.major_mode, uint32_t(info.explicit_major), info.path_select, info.tess_mode,
      info.edram_mode);
}

}  // namespace rex::graphics
