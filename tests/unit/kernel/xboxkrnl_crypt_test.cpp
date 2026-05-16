#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>

#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::kernel::xboxkrnl {
u32 XeCryptBnQwBeSigVerify_entry(mapped_void signature, mapped_void hash, mapped_void salt,
                                 mapped_void rsa);
void XeCryptRotSumSha_entry(mapped_void input_1, u32 input_1_size, mapped_void input_2,
                            u32 input_2_size, mapped_void output, u32 output_size);
u32 XeKeysGetKey_entry(u32 key_num, mapped_void key, mapped_u32 key_size);
u32 XeKeysConsolePrivateKeySign_entry(mapped_void hash, mapped_void signature);
}  // namespace rex::kernel::xboxkrnl

TEST_CASE("Crypto signature compatibility helpers are deterministic",
          "[kernel][xboxkrnl][crypto]") {
  constexpr rex::X_STATUS kSuccess = 0;

  std::array<uint8_t, 0x110> key{};
  key.fill(0xCD);
  rex::be_u32 key_size = 0x110;

  CHECK(rex::kernel::xboxkrnl::XeKeysGetKey_entry(
            57, mapped_void(key.data(), 0x40001000), mapped_u32(&key_size, 0x40002000)) ==
        kSuccess);
  CHECK(static_cast<uint32_t>(key_size) == 0x110);
  CHECK(std::all_of(key.begin(), key.end(), [](uint8_t value) { return value == 0; }));

  std::array<uint8_t, 1> signature{{0xA5}};
  std::array<uint8_t, 1> hash{{0x5A}};
  CHECK(rex::kernel::xboxkrnl::XeCryptBnQwBeSigVerify_entry(
            mapped_void(nullptr), mapped_void(hash.data(), 0x40004000), mapped_void(nullptr),
            mapped_void(key.data(), 0x40001000)) == 0);
  CHECK(rex::kernel::xboxkrnl::XeCryptBnQwBeSigVerify_entry(
            mapped_void(signature.data(), 0x40003000), mapped_void(hash.data(), 0x40004000),
            mapped_void(nullptr), mapped_void(key.data(), 0x40001000)) == 1);

  std::array<uint8_t, 2> input_1{{'a', 'b'}};
  std::array<uint8_t, 1> input_2{{'c'}};
  std::array<uint8_t, 20> digest{};
  digest.fill(0xCD);
  rex::kernel::xboxkrnl::XeCryptRotSumSha_entry(
      mapped_void(input_1.data(), 0x40005000), static_cast<u32>(input_1.size()),
      mapped_void(input_2.data(), 0x40006000), static_cast<u32>(input_2.size()),
      mapped_void(digest.data(), 0x40007000), static_cast<u32>(digest.size()));

  const std::array<uint8_t, 20> sha1_abc{{0xA9, 0x99, 0x3E, 0x36, 0x47, 0x06, 0x81,
                                         0x6A, 0xBA, 0x3E, 0x25, 0x71, 0x78, 0x50,
                                         0xC2, 0x6C, 0x9C, 0xD0, 0xD8, 0x9D}};
  CHECK(digest == sha1_abc);
}

TEST_CASE("Console private-key signer writes deterministic offline certificate",
          "[kernel][xboxkrnl][crypto]") {
  std::array<uint8_t, 0x14> hash{};
  hash.fill(0xA5);
  std::array<uint8_t, 0x1A8> certificate{};
  certificate.fill(0xCD);

  CHECK(rex::kernel::xboxkrnl::XeKeysConsolePrivateKeySign_entry(
            mapped_void(hash.data(), 0x40001000), mapped_void(certificate.data(), 0x40002000)) ==
        1);

  CHECK_FALSE(std::all_of(certificate.begin(), certificate.end(),
                          [](uint8_t value) { return value == 0; }));
  CHECK(certificate[0x18] == 0);
  CHECK(certificate[0x19] == 0);
  CHECK(certificate[0x1A] == 0);
  CHECK(certificate[0x1B] == 2);

  const std::array<uint8_t, 8> manufacture_date{{2, 0, 0, 5, 1, 1, 2, 2}};
  CHECK(std::equal(manufacture_date.begin(), manufacture_date.end(),
                   certificate.begin() + 0x1C));
  CHECK(std::all_of(certificate.begin() + 0x24, certificate.end(),
                    [](uint8_t value) { return value == 0; }));

  CHECK(rex::kernel::xboxkrnl::XeKeysConsolePrivateKeySign_entry(
            mapped_void(nullptr), mapped_void(certificate.data(), 0x40002000)) == 0);
  CHECK(rex::kernel::xboxkrnl::XeKeysConsolePrivateKeySign_entry(
            mapped_void(hash.data(), 0x40001000), mapped_void(nullptr)) == 0);
}
