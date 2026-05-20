#pragma once

#include <cstdint>
#include <string>

namespace rex::graphics {

enum class ShaderPipelineStage {
  kVertex,
  kPixel,
};

const char* BuildInvalidShaderDetail(ShaderPipelineStage stage, bool was_translated_before,
                                     bool async_shader_compilation);

struct IssueDrawFailureInfo {
  const char* backend = "";
  const char* stage = "";
  const char* detail = "";
  uint32_t prim_type = 0;
  uint32_t index_count = 0;
  uint32_t source_select = 0;
  uint32_t major_mode = 0;
  bool explicit_major = false;
  uint32_t path_select = 0;
  uint32_t tess_mode = 0;
  uint32_t edram_mode = 0;
  bool has_vertex_shader_hash = false;
  uint64_t vertex_shader_hash = 0;
  bool has_vertex_shader_modification = false;
  uint64_t vertex_shader_modification = 0;
  bool has_pixel_shader_hash = false;
  uint64_t pixel_shader_hash = 0;
  bool has_pixel_shader_modification = false;
  uint64_t pixel_shader_modification = 0;
  bool has_primitive_processing = false;
  uint32_t host_primitive_type = 0;
  uint32_t host_vertex_shader_type = 0;
  uint32_t host_draw_vertex_count = 0;
};

std::string FormatIssueDrawFailure(const IssueDrawFailureInfo& info);

}  // namespace rex::graphics
