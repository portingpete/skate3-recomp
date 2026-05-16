#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/filesystem/vfs.h>
#include <rex/system/xfile.h>

using rex::X_STATUS;

namespace {

class DirectoryEnumerationFile final : public rex::filesystem::File {
 public:
  explicit DirectoryEnumerationFile(rex::filesystem::Entry* entry)
      : File(rex::filesystem::FileAccess::kGenericRead, entry) {}

  void Destroy() override {}

  X_STATUS ReadSync(std::span<uint8_t> buffer, size_t byte_offset,
                    size_t* out_bytes_read) override {
    (void)buffer;
    (void)byte_offset;
    if (out_bytes_read) {
      *out_bytes_read = 0;
    }
    return X_STATUS_NOT_IMPLEMENTED;
  }

  X_STATUS WriteSync(std::span<const uint8_t> buffer, size_t byte_offset,
                     size_t* out_bytes_written) override {
    (void)buffer;
    (void)byte_offset;
    if (out_bytes_written) {
      *out_bytes_written = 0;
    }
    return X_STATUS_ACCESS_DENIED;
  }
};

}  // namespace

TEST_CASE("XFile directory queries report actual entry bytes", "[filesystem][vfs]") {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("rex_xfile_query_directory_bytes_" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::error_code cleanup_error;
  std::filesystem::remove_all(root, cleanup_error);
  std::filesystem::create_directories(root / "shaders");
  {
    std::ofstream file(root / "shaders" / "castle_shader_probe.bin", std::ios::binary);
    file << "shader";
  }

  rex::filesystem::VirtualFileSystem vfs;
  auto device = std::make_unique<rex::filesystem::HostPathDevice>("game:", root, true);
  REQUIRE(device->Initialize());
  REQUIRE(vfs.RegisterDevice(std::move(device)));

  auto* directory = vfs.ResolvePath("game:\\shaders");
  REQUIRE(directory != nullptr);

  DirectoryEnumerationFile directory_file(directory);
  rex::system::XFile xfile(nullptr, &directory_file, true);

  std::vector<uint8_t> buffer(512, 0xCD);
  auto* info = reinterpret_cast<rex::system::X_FILE_DIRECTORY_INFORMATION*>(buffer.data());
  size_t bytes_written = 0;
  REQUIRE(xfile.QueryDirectory(info, buffer.size(), "", true, &bytes_written) ==
          X_STATUS_SUCCESS);

  const size_t expected_bytes =
      rex::system::XFileDirectoryInformationSize(info->file_name_length);
  CHECK(bytes_written == expected_bytes);
  CHECK(bytes_written < buffer.size());
  CHECK(info->next_entry_offset == 0);
  CHECK(info->file_name_length == std::string_view("castle_shader_probe.bin").size());

  std::filesystem::remove_all(root, cleanup_error);
}
