#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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
u32 XNetLogonGetMachineID_entry(mapped_u64 machine_id_ptr);
u32 XNetLogonGetTitleID_entry(u32 caller);
u32 NetDll_XNetCreateKey_entry(u32 caller, mapped_void key_id, mapped_void key);
u32 NetDll_XNetRegisterKey_entry(u32 caller, mapped_void key_id, mapped_void key);
u32 NetDll_XNetReplaceKey_entry(u32 caller, mapped_void key_id, mapped_void key);
u32 NetDll_XNetUnregisterKey_entry(u32 caller, mapped_void key_id);
u32 NetDll_XNetConnect_entry(u32 caller, u32 in_addr);
u32 NetDll_XNetGetConnectStatus_entry(u32 caller, u32 in_addr);
u32 NetDll_XNetUnregisterInAddr_entry(u32 caller, u32 in_addr);
}  // namespace rex::kernel::xam

namespace {

constexpr u32 kSocketError = 0xFFFFFFFFu;
constexpr u32 kErrorIoPending = 0x000003E5u;
constexpr u64 kOfflineMachineId = 0xFA00000002CCCCCCull;

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

TEST_CASE("XNetLogonGetTitleID has a deterministic no-title fallback", "[kernel][xam_net]") {
  CHECK(rex::kernel::xam::XNetLogonGetTitleID_entry(2) == 0);
}

TEST_CASE("XNetLogonGetMachineID reports a deterministic offline machine id",
          "[kernel][xam_net]") {
  rex::be_u64 machine_id1 = 0;
  rex::be_u64 machine_id2 = 0;

  CHECK(rex::kernel::xam::XNetLogonGetMachineID_entry(
            mapped_u64(&machine_id1, 0x40001000)) == 0);
  CHECK(rex::kernel::xam::XNetLogonGetMachineID_entry(
            mapped_u64(&machine_id2, 0x40001008)) == 0);
  CHECK(u64(machine_id1) == kOfflineMachineId);
  CHECK(u64(machine_id2) == kOfflineMachineId);
  CHECK(rex::kernel::xam::XNetLogonGetMachineID_entry(mapped_u64(nullptr)) != 0);
}

TEST_CASE("XNet connection helpers report deterministic offline status", "[kernel][xam_net]") {
  constexpr u32 kCaller = 1;
  constexpr u32 kLoopback = 0x7F000001;

  CHECK(rex::kernel::xam::NetDll_XNetConnect_entry(kCaller, kLoopback) == 0);
  CHECK(rex::kernel::xam::NetDll_XNetGetConnectStatus_entry(kCaller, kLoopback) == 0);
  CHECK(rex::kernel::xam::NetDll_XNetUnregisterInAddr_entry(kCaller, kLoopback) == 0);
}

TEST_CASE("XNet key registration helpers are deterministic no-op successes",
          "[kernel][xam_net]") {
  constexpr u32 kCaller = 1;
  std::array<uint8_t, 8> key_id{};
  std::array<uint8_t, 16> key{};
  key_id.fill(0x11);
  key.fill(0x22);

  CHECK(rex::kernel::xam::NetDll_XNetCreateKey_entry(
            kCaller, mapped_void(key_id.data(), 0x40002000),
            mapped_void(key.data(), 0x40002010)) == 0);
  CHECK(std::all_of(key_id.begin(), key_id.end(), [](uint8_t value) {
    return value == 0xBB;
  }));
  CHECK(std::all_of(key.begin(), key.end(), [](uint8_t value) {
    return value == 0xBB;
  }));
  CHECK(rex::kernel::xam::NetDll_XNetRegisterKey_entry(
            kCaller, mapped_void(nullptr), mapped_void(nullptr)) == 0);
  CHECK(rex::kernel::xam::NetDll_XNetReplaceKey_entry(
            kCaller, mapped_void(nullptr), mapped_void(nullptr)) == 0);
  CHECK(rex::kernel::xam::NetDll_XNetUnregisterKey_entry(
            kCaller, mapped_void(key_id.data(), 0x40002000)) == 0);
}

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
