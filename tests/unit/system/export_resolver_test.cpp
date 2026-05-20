/**
 * @file        tests/unit/system/export_resolver_test.cpp
 * @brief       Unit tests for kernel export table resolution
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <rex/system/export_resolver.h>

namespace {

std::vector<rex::runtime::Export*> ExportTableFor(rex::runtime::Export& export_entry) {
  std::vector<rex::runtime::Export*> exports(export_entry.ordinal + 1);
  exports[export_entry.ordinal] = &export_entry;
  return exports;
}

}  // namespace

TEST_CASE("ExportResolver normalizes module names before ordinal lookup",
          "[runtime][exports]") {
  rex::runtime::ExportResolver resolver;
  rex::runtime::Export xam_export(1, rex::runtime::Export::Type::kFunction,
                                  "XamLoaderGetLaunchData");
  auto xam_exports = ExportTableFor(xam_export);

  resolver.RegisterTable("xam.xex", &xam_exports);

  CHECK(resolver.GetExportByOrdinal("xam", 1) == &xam_export);
  CHECK(resolver.GetExportByOrdinal("xam.xex", 1) == &xam_export);
  CHECK(resolver.GetExportByOrdinal("XAM.XEX", 1) == &xam_export);
}

TEST_CASE("ExportResolver does not let short module names shadow longer modules",
          "[runtime][exports]") {
  rex::runtime::ExportResolver resolver;
  rex::runtime::Export xam_export(1, rex::runtime::Export::Type::kFunction,
                                  "XamLoaderGetLaunchData");
  rex::runtime::Export xamx_export(1, rex::runtime::Export::Type::kFunction,
                                   "XamxDifferentExport");
  auto xam_exports = ExportTableFor(xam_export);
  auto xamx_exports = ExportTableFor(xamx_export);

  resolver.RegisterTable("xam.xex", &xam_exports);
  resolver.RegisterTable("xamx.xex", &xamx_exports);

  CHECK(resolver.GetExportByOrdinal("xamx.xex", 1) == &xamx_export);
  CHECK(resolver.GetExportByOrdinal("xamcache.xex", 1) == nullptr);
}
