#include <string>

#include <catch2/catch_test_macros.hpp>

#include <rex/graphics/draw_diagnostics.h>

TEST_CASE("IssueDraw failure diagnostics include backend stage and draw state",
          "[graphics][diagnostics]") {
  rex::graphics::IssueDrawFailureInfo info{
      .backend = "D3D12",
      .stage = "configure_pipeline",
      .detail = "state_description_root_signature_failed",
      .prim_type = 6,
      .index_count = 4,
      .source_select = 2,
      .major_mode = 0,
      .explicit_major = false,
      .path_select = 0,
      .tess_mode = 1,
      .edram_mode = 4,
      .has_vertex_shader_hash = true,
      .vertex_shader_hash = 0x0A6D1DD7767FDF27ull,
      .has_vertex_shader_modification = true,
      .vertex_shader_modification = 0x0000000000000003ull,
      .has_pixel_shader_hash = true,
      .pixel_shader_hash = 0xE14499F47375C20Bull,
      .has_pixel_shader_modification = true,
      .pixel_shader_modification = 0x0021400000010001ull,
      .has_primitive_processing = true,
      .host_primitive_type = 6,
      .host_vertex_shader_type = 0,
      .host_draw_vertex_count = 4,
  };

  const std::string text = rex::graphics::FormatIssueDrawFailure(info);

  CHECK(text.find("D3D12 IssueDraw failed at configure_pipeline") != std::string::npos);
  CHECK(text.find("detail=state_description_root_signature_failed") != std::string::npos);
  CHECK(text.find("prim_type=6") != std::string::npos);
  CHECK(text.find("index_count=4") != std::string::npos);
  CHECK(text.find("source_select=2") != std::string::npos);
  CHECK(text.find("major_mode=0") != std::string::npos);
  CHECK(text.find("explicit_major=0") != std::string::npos);
  CHECK(text.find("path_select=0") != std::string::npos);
  CHECK(text.find("tess_mode=1") != std::string::npos);
  CHECK(text.find("edram_mode=4") != std::string::npos);
  CHECK(text.find("vs=0x0A6D1DD7767FDF27") != std::string::npos);
  CHECK(text.find("vs_mod=0x0000000000000003") != std::string::npos);
  CHECK(text.find("ps=0xE14499F47375C20B") != std::string::npos);
  CHECK(text.find("ps_mod=0x0021400000010001") != std::string::npos);
  CHECK(text.find("host_primitive_type=6") != std::string::npos);
  CHECK(text.find("host_vertex_shader_type=0") != std::string::npos);
  CHECK(text.find("host_draw_vertex_count=4") != std::string::npos);
}

TEST_CASE("Invalid shader diagnostics distinguish preexisting async state",
          "[graphics][diagnostics]") {
  CHECK(rex::graphics::BuildInvalidShaderDetail(rex::graphics::ShaderPipelineStage::kPixel, true,
                                                true) ==
        std::string("pixel_shader_invalid_preexisting_async"));
  CHECK(rex::graphics::BuildInvalidShaderDetail(rex::graphics::ShaderPipelineStage::kPixel, true,
                                                false) ==
        std::string("pixel_shader_invalid_preexisting"));
  CHECK(rex::graphics::BuildInvalidShaderDetail(rex::graphics::ShaderPipelineStage::kVertex, false,
                                                false) ==
        std::string("vertex_shader_invalid_after_translation"));
}
