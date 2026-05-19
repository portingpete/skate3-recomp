#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <rex/cvar.h>
#include <rex/filesystem/entry.h>
#include <rex/kernel/init.h>
#include <rex/logging.h>
#include <rex/logging/sink.h>
#include <rex/memory.h>
#include <rex/runtime.h>
#include <rex/system/info/file.h>
#include <rex/system/xtypes.h>
#include <rex/system/xio.h>
#include <rex/types.h>

using rex::X_HANDLE;
using rex::X_STATUS;

namespace rex::kernel::xboxkrnl {
u32 IoDismountVolumeByFileHandle_entry(u32 handle);
u32 IoDismountVolumeByName_entry(ppc_ptr_t<rex::system::X_ANSI_STRING> name);
u32 NtCreateFile_entry(mapped_u32 handle_out, u32 desired_access,
                       ppc_ptr_t<rex::system::X_OBJECT_ATTRIBUTES> object_attrs,
                       ppc_ptr_t<rex::system::X_IO_STATUS_BLOCK> io_status_block,
                       mapped_u64 allocation_size_ptr, u32 file_attributes, u32 share_access,
                       u32 creation_disposition, u32 create_options);
u32 NtQueryFullAttributesFile_entry(
    ppc_ptr_t<rex::system::X_OBJECT_ATTRIBUTES> object_attrs,
    ppc_ptr_t<rex::system::X_FILE_NETWORK_OPEN_INFORMATION> file_info);
u32 NtQueryInformationFile_entry(u32 file_handle,
                                 ppc_ptr_t<rex::system::X_IO_STATUS_BLOCK> io_status_block,
                                 mapped_void info_ptr, u32 info_length, u32 info_class);
u32 StfsCreateDevice_entry(mapped_void device_object, u32 flags, mapped_u32 out_device);
u32 StfsControlDevice_entry(mapped_void device_object, u32 ioctl, mapped_void input_buffer,
                            u32 input_buffer_size, mapped_void output_buffer,
                            u32 output_buffer_size);
}  // namespace rex::kernel::xboxkrnl

namespace {
bool IsVolumeOrStfsNoOpLog(std::string_view text) {
  return text.find("IoDismountVolumeByFileHandle") != std::string_view::npos ||
         text.find("IoDismountVolumeByName") != std::string_view::npos ||
         text.find("StfsCreateDevice") != std::string_view::npos ||
         text.find("StfsControlDevice") != std::string_view::npos;
}

bool IsNtCreateFileMissingAssetLog(std::string_view text) {
  return text.find("NtCreateFile") != std::string_view::npos &&
         text.find("missing_optional_asset.bin") != std::string_view::npos &&
         text.find("0xc000000f") != std::string_view::npos;
}

bool IsVfsEntryNotFoundLog(std::string_view text) {
  return text.find("VFS: entry not found") != std::string_view::npos &&
         text.find("game:\\data") != std::string_view::npos;
}

bool IsShaderDumpProbeLog(std::string_view text) {
  return text.find("ShaderDumpxe:\\CompareBackEnds") != std::string_view::npos;
}

bool IsRelativeOptionsProbeLog(std::string_view text) {
  return text.find("Options.ini") != std::string_view::npos &&
         text.find("CACHE:") == std::string_view::npos;
}

bool IsNtCreateFileRelativeOptionsWriteProbeLog(std::string_view text) {
  return text.find("NtCreateFile") != std::string_view::npos &&
         text.find("Options.ini") != std::string_view::npos &&
         text.find("0xc0000022") != std::string_view::npos;
}

bool IsTitleDebugLogWriteProbeLog(std::string_view text) {
  return text.find("lhdebug.log") != std::string_view::npos &&
         text.find("0xc0000022") != std::string_view::npos;
}

bool IsReadOnlyTitleDebugLogProbeLog(std::string_view text) {
  return text.find("read-only file/dir") != std::string_view::npos &&
         text.find("D:\\lhdebug.log") != std::string_view::npos;
}

bool IsCacheBigProbeLog(std::string_view text) {
  return text.find("CACHE:\\big\\assets.0.big") != std::string_view::npos ||
         text.find("cache:\\big\\assets.0.big") != std::string_view::npos;
}

bool IsFileSectorInformationStubLog(std::string_view text) {
  return text.find("Stub XFileSectorInformation") != std::string_view::npos;
}

bool IsOptionalStorageRootProbeLog(std::string_view text) {
  constexpr std::array<std::string_view, 8> kProbeFragments = {
      "VFS: 'cache:\\'",
      "ResolvePath(cache:\\)",
      "VFS: 'cache1:\\'",
      "ResolvePath(cache1:\\)",
      "VFS: 'update:\\'",
      "ResolvePath(update:\\)",
      "VFS: 'update:\\update.img'",
      "ResolvePath(update:\\update.img)",
  };
  return std::any_of(kProbeFragments.begin(), kProbeFragments.end(),
                     [text](const std::string_view fragment) {
                       return text.find(fragment) != std::string_view::npos;
                     });
}

u32 StoreAnsiString(rex::memory::Memory* memory, const std::string_view value) {
  const u32 chars_guest = memory->SystemHeapAlloc(static_cast<u32>(value.size()));
  auto* chars = memory->TranslateVirtual<char*>(chars_guest);
  REQUIRE(chars != nullptr);
  std::memcpy(chars, value.data(), value.size());

  const u32 string_guest = memory->SystemHeapAlloc(sizeof(rex::system::X_ANSI_STRING));
  auto* string = memory->TranslateVirtual<rex::system::X_ANSI_STRING*>(string_guest);
  REQUIRE(string != nullptr);
  string->length = static_cast<u16>(value.size());
  string->maximum_length = static_cast<u16>(value.size());
  string->pointer = chars_guest;
  return string_guest;
}
}  // namespace

TEST_CASE("Shader dump backend probes miss devices without warning",
          "[runtime][kernel][xboxkrnl][io]") {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("rex_shaderdump_probe_log_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  std::filesystem::create_directories(root);

  rex::Runtime runtime(root, {}, {}, {});
  rex::RuntimeConfig config;
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == X_STATUS_SUCCESS);

  rex::InitLogging(nullptr, spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::fs(), spdlog::level::trace);

  auto fs_sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::fs(), fs_sink);

  auto* memory = runtime.kernel_state()->memory();
  const u32 path_guest = StoreAnsiString(memory, "ShaderDumpxe:\\CompareBackEnds");

  rex::system::X_OBJECT_ATTRIBUTES attrs{};
  attrs.root_directory = 0;
  attrs.name_ptr = path_guest;
  attrs.attributes = 0x40;

  rex::system::X_FILE_NETWORK_OPEN_INFORMATION file_info{};

  CHECK(rex::kernel::xboxkrnl::NtQueryFullAttributesFile_entry(
            ppc_ptr_t<rex::system::X_OBJECT_ATTRIBUTES>(&attrs, 0x40002000),
            ppc_ptr_t<rex::system::X_FILE_NETWORK_OPEN_INFORMATION>(&file_info, 0x40003000)) ==
        X_STATUS_NO_SUCH_FILE);

  std::vector<rex::LogEntry> fs_entries;
  fs_sink->CopyEntries(fs_entries);
  rex::RemoveSink(rex::log::fs(), fs_sink);

  const auto fs_warning_count =
      std::count_if(fs_entries.begin(), fs_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn && IsShaderDumpProbeLog(entry.text);
      });
  const auto fs_debug_count =
      std::count_if(fs_entries.begin(), fs_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level == spdlog::level::debug &&
               entry.text.find("ignored shader dump probe") != std::string_view::npos;
      });

  CHECK(fs_debug_count == 1);
  CHECK(fs_warning_count == 0);

  std::filesystem::remove_all(root, cleanup_error);
}

TEST_CASE("NtCreateFile missing file probes return not-found without warning",
          "[runtime][kernel][xboxkrnl][io]") {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("rex_ntcreatefile_missing_log_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  std::filesystem::create_directories(root);

  rex::Runtime runtime(root, {}, {}, {});
  rex::RuntimeConfig config;
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == X_STATUS_SUCCESS);

  rex::InitLogging(nullptr, spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::krnl(), spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::fs(), spdlog::level::trace);

  const bool old_noisy = REXCVAR_GET(log_noisy);
  REXCVAR_SET(log_noisy, true);

  auto krnl_sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::krnl(), krnl_sink);
  auto fs_sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::fs(), fs_sink);

  auto* memory = runtime.kernel_state()->memory();
  const u32 path_guest = StoreAnsiString(memory, "game:\\data\\missing_optional_asset.bin");

  rex::system::X_OBJECT_ATTRIBUTES attrs{};
  attrs.root_directory = 0;
  attrs.name_ptr = path_guest;
  attrs.attributes = 0x40;

  rex::system::X_IO_STATUS_BLOCK iosb{};
  rex::be_u32 handle = 0;

  CHECK(rex::kernel::xboxkrnl::NtCreateFile_entry(
            mapped_u32(&handle, 0x40001000), rex::filesystem::FileAccess::kGenericRead,
            ppc_ptr_t<rex::system::X_OBJECT_ATTRIBUTES>(&attrs, 0x40002000),
            ppc_ptr_t<rex::system::X_IO_STATUS_BLOCK>(&iosb, 0x40003000), mapped_u64(nullptr),
            rex::system::X_FILE_ATTRIBUTE_NORMAL, 1,
            static_cast<u32>(rex::filesystem::FileDisposition::kOpen), 0) ==
        X_STATUS_NO_SUCH_FILE);
  CHECK(static_cast<u32>(iosb.status) == X_STATUS_NO_SUCH_FILE);
  CHECK(static_cast<u32>(iosb.information) ==
        static_cast<u32>(rex::filesystem::FileAction::kDoesNotExist));
  CHECK(static_cast<u32>(handle) == X_INVALID_HANDLE_VALUE);

  std::vector<rex::LogEntry> krnl_entries;
  krnl_sink->CopyEntries(krnl_entries);
  rex::RemoveSink(rex::log::krnl(), krnl_sink);
  std::vector<rex::LogEntry> fs_entries;
  fs_sink->CopyEntries(fs_entries);
  rex::RemoveSink(rex::log::fs(), fs_sink);
  REXCVAR_SET(log_noisy, old_noisy);

  const auto krnl_warning_count =
      std::count_if(krnl_entries.begin(), krnl_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn && IsNtCreateFileMissingAssetLog(entry.text);
      });
  const auto krnl_debug_count =
      std::count_if(krnl_entries.begin(), krnl_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level == spdlog::level::debug && IsNtCreateFileMissingAssetLog(entry.text);
      });
  const auto fs_warning_count =
      std::count_if(fs_entries.begin(), fs_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn && IsVfsEntryNotFoundLog(entry.text);
      });
  const auto fs_debug_count =
      std::count_if(fs_entries.begin(), fs_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level == spdlog::level::debug && IsVfsEntryNotFoundLog(entry.text);
      });

  CHECK(krnl_debug_count == 1);
  CHECK(krnl_warning_count == 0);
  CHECK(fs_debug_count == 1);
  CHECK(fs_warning_count == 0);

  std::filesystem::remove_all(root, cleanup_error);
}

TEST_CASE("XFileSectorInformation returns a stable token without stub noise",
          "[runtime][kernel][xboxkrnl][io]") {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("rex_file_sector_info_log_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  std::filesystem::create_directories(root / "data");
  {
    std::ofstream file(root / "data" / "sector.bin", std::ios::binary);
    REQUIRE(file.good());
    file << "sector-info";
  }

  rex::Runtime runtime(root, {}, {}, {});
  rex::RuntimeConfig config;
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == X_STATUS_SUCCESS);

  rex::InitLogging(nullptr, spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::krnl(), spdlog::level::trace);

  auto krnl_sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::krnl(), krnl_sink);

  auto* memory = runtime.kernel_state()->memory();
  const u32 path_guest = StoreAnsiString(memory, "game:\\data\\sector.bin");

  rex::system::X_OBJECT_ATTRIBUTES attrs{};
  attrs.root_directory = 0;
  attrs.name_ptr = path_guest;
  attrs.attributes = 0x40;

  rex::system::X_IO_STATUS_BLOCK create_iosb{};
  rex::be_u32 handle = X_INVALID_HANDLE_VALUE;
  REQUIRE(rex::kernel::xboxkrnl::NtCreateFile_entry(
              mapped_u32(&handle, 0x40001000), rex::filesystem::FileAccess::kGenericRead,
              ppc_ptr_t<rex::system::X_OBJECT_ATTRIBUTES>(&attrs, 0x40002000),
              ppc_ptr_t<rex::system::X_IO_STATUS_BLOCK>(&create_iosb, 0x40003000),
              mapped_u64(nullptr), rex::system::X_FILE_ATTRIBUTE_NORMAL, 1,
              static_cast<u32>(rex::filesystem::FileDisposition::kOpen), 0) ==
          X_STATUS_SUCCESS);

  rex::system::X_IO_STATUS_BLOCK query_iosb{};
  rex::be_u32 sector_token = 0;
  CHECK(rex::kernel::xboxkrnl::NtQueryInformationFile_entry(
            static_cast<u32>(handle),
            ppc_ptr_t<rex::system::X_IO_STATUS_BLOCK>(&query_iosb, 0x40004000),
            mapped_void(&sector_token, 0x40005000), sizeof(sector_token),
            rex::system::XFileSectorInformation) == X_STATUS_SUCCESS);
  CHECK(static_cast<u32>(query_iosb.status) == X_STATUS_SUCCESS);
  CHECK(static_cast<u32>(query_iosb.information) == sizeof(sector_token));
  CHECK(static_cast<u32>(sector_token) != 0);

  std::vector<rex::LogEntry> krnl_entries;
  krnl_sink->CopyEntries(krnl_entries);
  rex::RemoveSink(rex::log::krnl(), krnl_sink);

  const auto stub_debug_count =
      std::count_if(krnl_entries.begin(), krnl_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level == spdlog::level::debug && IsFileSectorInformationStubLog(entry.text);
      });
  CHECK(stub_debug_count == 0);

  std::filesystem::remove_all(root, cleanup_error);
}

TEST_CASE("Bare relative file probes miss devices without warning",
          "[runtime][kernel][xboxkrnl][io]") {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("rex_relative_file_probe_log_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  std::filesystem::create_directories(root);

  rex::Runtime runtime(root, {}, {}, {});
  rex::RuntimeConfig config;
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == X_STATUS_SUCCESS);

  rex::InitLogging(nullptr, spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::fs(), spdlog::level::trace);

  auto fs_sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::fs(), fs_sink);

  CHECK(runtime.kernel_state()->file_system()->ResolvePath("Options.ini") == nullptr);
  CHECK(runtime.kernel_state()->file_system()->ResolvePath("CACHE:\\Options.ini") == nullptr);

  std::vector<rex::LogEntry> fs_entries;
  fs_sink->CopyEntries(fs_entries);
  rex::RemoveSink(rex::log::fs(), fs_sink);

  const auto relative_warning_count =
      std::count_if(fs_entries.begin(), fs_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn && IsRelativeOptionsProbeLog(entry.text);
      });
  const auto relative_debug_count =
      std::count_if(fs_entries.begin(), fs_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level == spdlog::level::debug &&
               entry.text.find("relative file probe") != std::string_view::npos &&
               IsRelativeOptionsProbeLog(entry.text);
      });
  const auto device_warning_count =
      std::count_if(fs_entries.begin(), fs_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn &&
               entry.text.find("CACHE:\\Options.ini") != std::string_view::npos;
      });

  CHECK(relative_debug_count == 1);
  CHECK(relative_warning_count == 0);
  CHECK(device_warning_count >= 1);

  std::filesystem::remove_all(root, cleanup_error);
}

TEST_CASE("BFME2 Options.ini write probe fails without warning",
          "[runtime][kernel][xboxkrnl][io]") {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("rex_relative_options_write_probe_log_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  std::filesystem::create_directories(root);

  rex::Runtime runtime(root, {}, {}, {});
  rex::RuntimeConfig config;
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == X_STATUS_SUCCESS);

  rex::InitLogging(nullptr, spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::krnl(), spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::fs(), spdlog::level::trace);

  const bool old_noisy = REXCVAR_GET(log_noisy);
  REXCVAR_SET(log_noisy, true);

  auto krnl_sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::krnl(), krnl_sink);
  auto fs_sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::fs(), fs_sink);

  auto* memory = runtime.kernel_state()->memory();
  const u32 path_guest = StoreAnsiString(memory, "Options.ini");

  rex::system::X_OBJECT_ATTRIBUTES attrs{};
  attrs.root_directory = 0xFFFFFFFDu;
  attrs.name_ptr = path_guest;
  attrs.attributes = 0x40;

  rex::system::X_IO_STATUS_BLOCK iosb{};
  rex::be_u32 handle = 0;

  CHECK(rex::kernel::xboxkrnl::NtCreateFile_entry(
            mapped_u32(&handle, 0x40001000), 0x40100080u,
            ppc_ptr_t<rex::system::X_OBJECT_ATTRIBUTES>(&attrs, 0x40002000),
            ppc_ptr_t<rex::system::X_IO_STATUS_BLOCK>(&iosb, 0x40003000), mapped_u64(nullptr),
            rex::system::X_FILE_ATTRIBUTE_NORMAL, 3,
            static_cast<u32>(rex::filesystem::FileDisposition::kOverwriteIf), 0x60u) ==
        X_STATUS_ACCESS_DENIED);
  CHECK(static_cast<u32>(iosb.status) == X_STATUS_ACCESS_DENIED);
  CHECK(static_cast<u32>(handle) == X_INVALID_HANDLE_VALUE);

  std::vector<rex::LogEntry> krnl_entries;
  krnl_sink->CopyEntries(krnl_entries);
  rex::RemoveSink(rex::log::krnl(), krnl_sink);
  std::vector<rex::LogEntry> fs_entries;
  fs_sink->CopyEntries(fs_entries);
  rex::RemoveSink(rex::log::fs(), fs_sink);
  REXCVAR_SET(log_noisy, old_noisy);

  const auto krnl_warning_count =
      std::count_if(krnl_entries.begin(), krnl_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn &&
               IsNtCreateFileRelativeOptionsWriteProbeLog(entry.text);
      });
  const auto krnl_debug_count =
      std::count_if(krnl_entries.begin(), krnl_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level == spdlog::level::debug &&
               IsNtCreateFileRelativeOptionsWriteProbeLog(entry.text);
      });
  const auto fs_warning_count =
      std::count_if(fs_entries.begin(), fs_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn && IsRelativeOptionsProbeLog(entry.text);
      });
  const auto fs_debug_count =
      std::count_if(fs_entries.begin(), fs_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level == spdlog::level::debug &&
               entry.text.find("relative file probe") != std::string_view::npos &&
               IsRelativeOptionsProbeLog(entry.text);
      });

  CHECK(krnl_debug_count == 1);
  CHECK(krnl_warning_count == 0);
  CHECK(fs_debug_count >= 1);
  CHECK(fs_warning_count == 0);

  std::filesystem::remove_all(root, cleanup_error);
}

TEST_CASE("Fable2 title debug log write probe fails without warning",
          "[runtime][kernel][xboxkrnl][io]") {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("rex_title_debug_log_probe_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  std::filesystem::create_directories(root);

  rex::Runtime runtime(root, {}, {}, {});
  rex::RuntimeConfig config;
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == X_STATUS_SUCCESS);

  rex::InitLogging(nullptr, spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::krnl(), spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::fs(), spdlog::level::trace);

  const bool old_noisy = REXCVAR_GET(log_noisy);
  REXCVAR_SET(log_noisy, true);

  auto krnl_sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::krnl(), krnl_sink);
  auto fs_sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::fs(), fs_sink);

  auto* memory = runtime.kernel_state()->memory();
  const u32 path_guest = StoreAnsiString(memory, "D:\\lhdebug.log");

  constexpr std::array<u32, 2> kRootHandles = {
      0,
      0xFFFFFFFDu,
  };
  for (const u32 root_handle : kRootHandles) {
    rex::system::X_OBJECT_ATTRIBUTES attrs{};
    attrs.root_directory = root_handle;
    attrs.name_ptr = path_guest;
    attrs.attributes = 0x40;

    rex::system::X_IO_STATUS_BLOCK iosb{};
    rex::be_u32 handle = 0;

    CHECK(rex::kernel::xboxkrnl::NtCreateFile_entry(
              mapped_u32(&handle, 0x40001000), 0x40100080u,
              ppc_ptr_t<rex::system::X_OBJECT_ATTRIBUTES>(&attrs, 0x40002000),
              ppc_ptr_t<rex::system::X_IO_STATUS_BLOCK>(&iosb, 0x40003000), mapped_u64(nullptr),
              rex::system::X_FILE_ATTRIBUTE_NORMAL, 3,
              static_cast<u32>(rex::filesystem::FileDisposition::kOverwriteIf), 0x60u) ==
          X_STATUS_ACCESS_DENIED);
    CHECK(static_cast<u32>(iosb.status) == X_STATUS_ACCESS_DENIED);
    CHECK(static_cast<u32>(handle) == X_INVALID_HANDLE_VALUE);
  }

  std::vector<rex::LogEntry> krnl_entries;
  krnl_sink->CopyEntries(krnl_entries);
  rex::RemoveSink(rex::log::krnl(), krnl_sink);
  std::vector<rex::LogEntry> fs_entries;
  fs_sink->CopyEntries(fs_entries);
  rex::RemoveSink(rex::log::fs(), fs_sink);
  REXCVAR_SET(log_noisy, old_noisy);

  const auto krnl_warning_count =
      std::count_if(krnl_entries.begin(), krnl_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn && IsTitleDebugLogWriteProbeLog(entry.text);
      });
  const auto krnl_debug_count =
      std::count_if(krnl_entries.begin(), krnl_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level == spdlog::level::debug && IsTitleDebugLogWriteProbeLog(entry.text);
      });
  const auto fs_warning_count =
      std::count_if(fs_entries.begin(), fs_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn &&
               IsReadOnlyTitleDebugLogProbeLog(entry.text);
      });
  const auto fs_debug_count =
      std::count_if(fs_entries.begin(), fs_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level == spdlog::level::debug &&
               IsReadOnlyTitleDebugLogProbeLog(entry.text);
      });

  CHECK(krnl_debug_count == 2);
  CHECK(krnl_warning_count == 0);
  CHECK(fs_debug_count == 2);
  CHECK(fs_warning_count == 0);

  std::filesystem::remove_all(root, cleanup_error);
}

TEST_CASE("Cache big fallback probes miss devices without warning",
          "[runtime][kernel][xboxkrnl][io]") {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("rex_cache_big_probe_log_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  std::filesystem::create_directories(root);

  rex::Runtime runtime(root, {}, {}, {});
  rex::RuntimeConfig config;
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == X_STATUS_SUCCESS);

  rex::InitLogging(nullptr, spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::fs(), spdlog::level::trace);

  auto fs_sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::fs(), fs_sink);

  auto* fs = runtime.kernel_state()->file_system();
  REQUIRE(fs->RegisterSymbolicLink("cache:", "\\Device\\cache0"));
  CHECK(fs->ResolvePath("CACHE:\\big\\assets.0.big") == nullptr);
  CHECK(fs->ResolvePath("CACHE:\\Options.ini") == nullptr);

  std::vector<rex::LogEntry> fs_entries;
  fs_sink->CopyEntries(fs_entries);
  rex::RemoveSink(rex::log::fs(), fs_sink);

  const auto cache_big_warning_count =
      std::count_if(fs_entries.begin(), fs_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn && IsCacheBigProbeLog(entry.text);
      });
  const auto cache_big_debug_count =
      std::count_if(fs_entries.begin(), fs_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level == spdlog::level::debug && IsCacheBigProbeLog(entry.text) &&
               entry.text.find("cache fallback probe") != std::string_view::npos;
      });
  const auto device_warning_count =
      std::count_if(fs_entries.begin(), fs_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn &&
               entry.text.find("CACHE:\\Options.ini") != std::string_view::npos;
      });

  CHECK(cache_big_debug_count == 1);
  CHECK(cache_big_warning_count == 0);
  CHECK(device_warning_count >= 1);

  std::filesystem::remove_all(root, cleanup_error);
}

TEST_CASE("Optional storage root probes miss devices without warning",
          "[runtime][kernel][xboxkrnl][io]") {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("rex_optional_storage_probe_log_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  std::filesystem::create_directories(root);

  rex::Runtime runtime(root, {}, {}, {});
  rex::RuntimeConfig config;
  config.tool_mode = true;
  config.kernel_init = rex::kernel::InitializeKernel;
  REQUIRE(runtime.Setup(std::move(config)) == X_STATUS_SUCCESS);

  rex::InitLogging(nullptr, spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::fs(), spdlog::level::trace);

  auto fs_sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::fs(), fs_sink);

  auto* fs = runtime.kernel_state()->file_system();
  CHECK(fs->ResolvePath("cache:\\") == nullptr);
  CHECK(fs->ResolvePath("cache1:\\") == nullptr);
  CHECK(fs->ResolvePath("update:\\") == nullptr);
  CHECK(fs->ResolvePath("update:\\update.img") == nullptr);
  CHECK(fs->ResolvePath("update:\\data\\effects") == nullptr);

  std::vector<rex::LogEntry> fs_entries;
  fs_sink->CopyEntries(fs_entries);
  rex::RemoveSink(rex::log::fs(), fs_sink);

  const auto optional_warning_count =
      std::count_if(fs_entries.begin(), fs_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn && IsOptionalStorageRootProbeLog(entry.text);
      });
  const auto optional_debug_count =
      std::count_if(fs_entries.begin(), fs_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level == spdlog::level::debug &&
               entry.text.find("optional storage probe") != std::string_view::npos;
      });
  const auto update_content_warning_count =
      std::count_if(fs_entries.begin(), fs_entries.end(), [](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn &&
               entry.text.find("update:\\data\\effects") != std::string_view::npos;
      });

  CHECK(optional_debug_count == 4);
  CHECK(optional_warning_count == 0);
  CHECK(update_content_warning_count >= 1);

  std::filesystem::remove_all(root, cleanup_error);
}

TEST_CASE("Volume and STFS no-op helpers are trace-only compatibility shims",
          "[runtime][kernel][xboxkrnl][io]") {
  rex::InitLogging(nullptr, spdlog::level::trace);
  rex::SetCategoryLevel(rex::log::krnl(), spdlog::level::trace);

  const bool old_noisy = REXCVAR_GET(log_noisy);
  REXCVAR_SET(log_noisy, true);

  auto sink = std::make_shared<rex::LogCaptureSink>();
  rex::AddSink(rex::log::krnl(), sink);

  rex::be_u32 out_device = 0xBEEFBEEFu;
  std::array<uint8_t, 4> input{0x11, 0x22, 0x33, 0x44};
  std::array<uint8_t, 4> output{0xCD, 0xCD, 0xCD, 0xCD};

  REXKRNL_DEBUG("Volume and STFS capture sentinel");
  CHECK(rex::kernel::xboxkrnl::IoDismountVolumeByFileHandle_entry(0xFEEDC0DE) == 0);
  CHECK(rex::kernel::xboxkrnl::IoDismountVolumeByName_entry(
            ppc_ptr_t<rex::system::X_ANSI_STRING>(nullptr)) == 0);
  CHECK(rex::kernel::xboxkrnl::StfsCreateDevice_entry(
            mapped_void(reinterpret_cast<void*>(uintptr_t{0x1}), 0x40001000), 0x58,
            mapped_u32(&out_device, 0x40002000)) == 0);
  CHECK(rex::kernel::xboxkrnl::StfsControlDevice_entry(
            mapped_void(reinterpret_cast<void*>(uintptr_t{0x1}), 0x40001000), 4,
            mapped_void(input.data(), 0x40003000), static_cast<u32>(input.size()),
            mapped_void(output.data(), 0x40004000), static_cast<u32>(output.size())) == 0);
  CHECK(out_device == 0xBEEFBEEFu);
  CHECK(output == std::array<uint8_t, 4>{0xCD, 0xCD, 0xCD, 0xCD});

  std::vector<rex::LogEntry> entries;
  sink->CopyEntries(entries);
  rex::RemoveSink(rex::log::krnl(), sink);
  REXCVAR_SET(log_noisy, old_noisy);

  CHECK(std::any_of(entries.begin(), entries.end(), [](const rex::LogEntry& entry) {
    return entry.text.find("Volume and STFS capture sentinel") != std::string::npos;
  }));

  const auto warning_count =
      std::count_if(entries.begin(), entries.end(), [](const rex::LogEntry& entry) {
        return entry.level >= spdlog::level::warn && IsVolumeOrStfsNoOpLog(entry.text);
      });
  CHECK(warning_count == 0);
}
