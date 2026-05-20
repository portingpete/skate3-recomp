#include <rex/graphics/draw_diagnostics.h>

#include <fmt/format.h>

namespace rex::graphics {

std::string FormatIssueDrawFailure(const IssueDrawFailureInfo& info) {
  std::string text = fmt::format(
      "{} IssueDraw failed at {} "
      "(prim_type={}, index_count={}, source_select={}, major_mode={}, explicit_major={}, "
      "path_select={}, tess_mode={}, edram_mode={}",
      info.backend, info.stage, info.prim_type, info.index_count, info.source_select,
      info.major_mode, uint32_t(info.explicit_major), info.path_select, info.tess_mode,
      info.edram_mode);
  if (info.has_vertex_shader_hash) {
    text += fmt::format(", vs=0x{:016X}", info.vertex_shader_hash);
  }
  if (info.has_pixel_shader_hash) {
    text += fmt::format(", ps=0x{:016X}", info.pixel_shader_hash);
  }
  if (info.has_primitive_processing) {
    text += fmt::format(", host_primitive_type={}, host_vertex_shader_type={}, "
                        "host_draw_vertex_count={}",
                        info.host_primitive_type, info.host_vertex_shader_type,
                        info.host_draw_vertex_count);
  }
  text.push_back(')');
  return text;
}

}  // namespace rex::graphics
