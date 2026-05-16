#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::system {
struct XSOCKADDR_IN;
}  // namespace rex::system

namespace rex::kernel::xam {
struct XWSABUF;
struct XWSAOVERLAPPED;

u32 NetDll_WSARecvFrom_entry(u32 caller, u32 socket, ppc_ptr_t<XWSABUF> buffers_ptr,
                             u32 buffer_count, mapped_u32 num_bytes_recv,
                             mapped_u32 flags_ptr, ppc_ptr_t<rex::system::XSOCKADDR_IN> from_addr,
                             ppc_ptr_t<XWSAOVERLAPPED> overlapped_ptr,
                             mapped_void completion_routine_ptr);
u32 NetDll_WSARecv_entry(u32 caller, u32 socket, ppc_ptr_t<XWSABUF> buffers_ptr,
                         u32 buffer_count, mapped_u32 num_bytes_recv, mapped_u32 flags_ptr,
                         ppc_ptr_t<XWSAOVERLAPPED> overlapped_ptr,
                         mapped_void completion_routine_ptr);
u32 NetDll_WSAGetOverlappedResult_entry(u32 caller, u32 socket,
                                        ppc_ptr_t<XWSAOVERLAPPED> overlapped_ptr,
                                        mapped_u32 num_bytes_transferred, u32 wait,
                                        mapped_u32 flags_ptr);
}  // namespace rex::kernel::xam

namespace {

constexpr u32 kSocketError = 0xFFFFFFFFu;
constexpr u32 kErrorIoPending = 0x000003E5u;

u32 ReadBe32(const uint8_t* data, size_t offset) {
  return (u32(data[offset]) << 24) | (u32(data[offset + 1]) << 16) |
         (u32(data[offset + 2]) << 8) | u32(data[offset + 3]);
}

void WriteBe32(uint8_t* data, size_t offset, u32 value) {
  data[offset] = uint8_t(value >> 24);
  data[offset + 1] = uint8_t(value >> 16);
  data[offset + 2] = uint8_t(value >> 8);
  data[offset + 3] = uint8_t(value);
}

}  // namespace

TEST_CASE("WSARecvFrom marks offline overlapped receives pending", "[kernel][xam_net]") {
  constexpr u32 kBytesRecv = 0x10;
  constexpr u32 kFlags = 0x14;
  constexpr u32 kOverlapped = 0x20;
  std::array<uint8_t, 0x80> storage{};
  WriteBe32(storage.data(), kBytesRecv, 0xFEEDF00D);
  WriteBe32(storage.data(), kFlags, 0xA5A5A5A5);
  WriteBe32(storage.data(), kOverlapped + 0, 0);
  WriteBe32(storage.data(), kOverlapped + 4, 0x11111111);

  auto overlapped = ppc_ptr_t<rex::kernel::xam::XWSAOVERLAPPED>(
      reinterpret_cast<rex::kernel::xam::XWSAOVERLAPPED*>(storage.data() + kOverlapped),
      kOverlapped);

  CHECK(rex::kernel::xam::NetDll_WSARecvFrom_entry(
            1, 0x1234, ppc_ptr_t<rex::kernel::xam::XWSABUF>(nullptr), 0,
            mapped_u32(reinterpret_cast<rex::be_u32*>(storage.data() + kBytesRecv), kBytesRecv),
            mapped_u32(reinterpret_cast<rex::be_u32*>(storage.data() + kFlags), kFlags),
            ppc_ptr_t<rex::system::XSOCKADDR_IN>(nullptr), overlapped,
            mapped_void(nullptr)) == kSocketError);
  CHECK(ReadBe32(storage.data(), kBytesRecv) == 0);
  CHECK(ReadBe32(storage.data(), kFlags) == 0xA5A5A5A5);
  CHECK(ReadBe32(storage.data(), kOverlapped + 0) == kErrorIoPending);
  CHECK(ReadBe32(storage.data(), kOverlapped + 4) == 0);
}

TEST_CASE("WSARecv mirrors the offline pending receive fallback", "[kernel][xam_net]") {
  constexpr u32 kBytesRecv = 0x10;
  constexpr u32 kOverlapped = 0x20;
  std::array<uint8_t, 0x80> storage{};
  WriteBe32(storage.data(), kBytesRecv, 0xFEEDF00D);
  WriteBe32(storage.data(), kOverlapped + 0, 0);
  WriteBe32(storage.data(), kOverlapped + 4, 0x11111111);

  auto overlapped = ppc_ptr_t<rex::kernel::xam::XWSAOVERLAPPED>(
      reinterpret_cast<rex::kernel::xam::XWSAOVERLAPPED*>(storage.data() + kOverlapped),
      kOverlapped);

  CHECK(rex::kernel::xam::NetDll_WSARecv_entry(
            1, 0x1234, ppc_ptr_t<rex::kernel::xam::XWSABUF>(nullptr), 0,
            mapped_u32(reinterpret_cast<rex::be_u32*>(storage.data() + kBytesRecv), kBytesRecv),
            mapped_u32(nullptr), overlapped, mapped_void(nullptr)) == kSocketError);
  CHECK(ReadBe32(storage.data(), kBytesRecv) == 0);
  CHECK(ReadBe32(storage.data(), kOverlapped + 0) == kErrorIoPending);
  CHECK(ReadBe32(storage.data(), kOverlapped + 4) == 0);
}

TEST_CASE("WSAGetOverlappedResult reports pending offline receives as incomplete",
          "[kernel][xam_net]") {
  constexpr u32 kTransferred = 0x10;
  constexpr u32 kFlags = 0x14;
  constexpr u32 kOverlapped = 0x20;
  std::array<uint8_t, 0x80> storage{};
  WriteBe32(storage.data(), kTransferred, 0xCCCCCCCC);
  WriteBe32(storage.data(), kFlags, 0xDDDDDDDD);
  WriteBe32(storage.data(), kOverlapped + 0, kErrorIoPending);
  WriteBe32(storage.data(), kOverlapped + 4, 0);

  auto overlapped = ppc_ptr_t<rex::kernel::xam::XWSAOVERLAPPED>(
      reinterpret_cast<rex::kernel::xam::XWSAOVERLAPPED*>(storage.data() + kOverlapped),
      kOverlapped);

  CHECK(rex::kernel::xam::NetDll_WSAGetOverlappedResult_entry(
            1, 0x1234, overlapped,
            mapped_u32(reinterpret_cast<rex::be_u32*>(storage.data() + kTransferred),
                       kTransferred),
            0, mapped_u32(reinterpret_cast<rex::be_u32*>(storage.data() + kFlags), kFlags)) ==
        0);
  CHECK(ReadBe32(storage.data(), kTransferred) == 0);
  CHECK(ReadBe32(storage.data(), kFlags) == 0);
}
