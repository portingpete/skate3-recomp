/**
 * @file        pipeline_util_test.cpp
 * @brief       Unit tests for shared graphics pipeline helpers
 *
 * @copyright   Copyright (c) 2026 Tom Clay
 * @license     BSD 3-Clause License
 */

#include <catch2/catch_test_macros.hpp>

#include <rex/graphics/pipeline_util.h>

TEST_CASE("Pipeline creation status distinguishes pending and failed null states",
          "[graphics][pipeline]") {
  using rex::graphics::pipeline_util::PipelineCreationStatus;

  CHECK(rex::graphics::pipeline_util::GetPipelineCreationStatus(false, false) ==
        PipelineCreationStatus::kPending);
  CHECK(rex::graphics::pipeline_util::GetPipelineCreationStatus(false, true) ==
        PipelineCreationStatus::kFailed);
  CHECK(rex::graphics::pipeline_util::GetPipelineCreationStatus(true, false) ==
        PipelineCreationStatus::kReady);
  CHECK(rex::graphics::pipeline_util::GetPipelineCreationStatus(true, true) ==
        PipelineCreationStatus::kReady);
}

TEST_CASE("Pipeline storage preload thread target uses temporary workers",
          "[graphics][pipeline]") {
  CHECK(rex::graphics::pipeline_util::GetPipelineStorageCreationThreadTarget(0, 8, 0) == 0);
  CHECK(rex::graphics::pipeline_util::GetPipelineStorageCreationThreadTarget(1, 8, 0) == 0);
  CHECK(rex::graphics::pipeline_util::GetPipelineStorageCreationThreadTarget(4, 8, 0) == 3);
  CHECK(rex::graphics::pipeline_util::GetPipelineStorageCreationThreadTarget(16, 8, 2) == 7);
  CHECK(rex::graphics::pipeline_util::GetPipelineStorageCreationThreadTarget(2, 8, 6) == 6);
  CHECK(rex::graphics::pipeline_util::GetPipelineStorageCreationThreadTarget(8, 1, 0) == 0);
}
