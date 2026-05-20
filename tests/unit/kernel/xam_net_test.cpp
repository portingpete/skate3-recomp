#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

#include <rex/system/xtypes.h>
#include <rex/types.h>

namespace rex::system {
struct XSOCKADDR;
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
u32 NetDll_XNetGetOpt_entry(u32 caller, u32 option_id, mapped_void buffer_ptr,
                            mapped_u32 buffer_size);
u32 NetDll_XNetSetOpt_entry(u32 caller, u32 option_id, mapped_void buffer_ptr,
                            u32 buffer_size);
u32 NetDll_XNetServerToInAddr_entry(u32 caller, mapped_void server, mapped_void xid,
                                    mapped_void in_addr);
u32 NetDll_XNetInAddrToServer_entry(u32 caller, mapped_void in_addr, mapped_void xid,
                                    mapped_void server);
u32 NetDll_XNetTsAddrToInAddr_entry(u32 caller, mapped_void ts_addr, mapped_void xid,
                                    mapped_void in_addr);
u32 NetDll_XNetGetBroadcastVersionStatus_entry(u32 caller, mapped_u32 status_ptr);
u32 NetDll_XNetQosGetListenStats_entry(u32 caller, mapped_void id, mapped_void stats_ptr,
                                       u32 stats_size);
u32 NetDll_XNetQosLookup_entry(u32 caller, u32 xnaddr_count, mapped_void xnaddr_ptrs,
                               mapped_void xnkid_ptrs, mapped_void xnkey_ptrs,
                               u32 inaddr_count, mapped_void inaddr_ptrs, mapped_void ports,
                               u32 probe_count, u32 bits_per_second, u32 flags,
                               u32 event_handle, mapped_u32 qos_out);
u32 NetDll_getsockopt_entry(u32 caller, u32 socket_handle, u32 level, u32 optname,
                            mapped_void optval_ptr, mapped_u32 optlen_ptr);
u32 NetDll_getsockname_entry(u32 caller, u32 socket_handle,
                             ppc_ptr_t<rex::system::XSOCKADDR> name,
                             mapped_u32 namelen_ptr);
u32 NetDll_getpeername_entry(u32 caller, u32 socket_handle,
                             ppc_ptr_t<rex::system::XSOCKADDR> name,
                             mapped_u32 namelen_ptr);

u32 NetDll_WSACancelOverlappedIO_entry(u32 caller, u32 socket_handle);
u32 NetDll_WSAEventSelect_entry(u32 caller, u32 socket_handle, u32 event_handle,
                                i32 network_events);
u32 NetDll_WSASend_entry(u32 caller, u32 socket_handle, ppc_ptr_t<XWSABUF> buffers,
                         u32 buffer_count, mapped_u32 num_bytes_sent, u32 flags,
                         ppc_ptr_t<XWSAOVERLAPPED> overlapped, mapped_void completion_routine);
namespace internal {
bool TryConvertWsaTimeoutToNtWaitTimeout(u32 timeout_ms, uint64_t* out_timeout);
}  // namespace internal
}  // namespace rex::kernel::xam

namespace {

constexpr u32 kSocketError = 0xFFFFFFFFu;
constexpr u32 kErrorIoPending = 0x000003E5u;
constexpr u64 kOfflineMachineId = 0xFA00000002CCCCCCull;
constexpr u32 kWsaEMsgSize = 0x00002738u;
constexpr u32 kWsaEInval = 0x00002726u;

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

TEST_CASE("WSAWaitForMultipleEvents converts finite timeouts to NT wait ticks",
          "[kernel][xam_net]") {
  uint64_t timeout = 0;

  REQUIRE(rex::kernel::xam::internal::TryConvertWsaTimeoutToNtWaitTimeout(1000, &timeout));
  CHECK(static_cast<int64_t>(timeout) == -10000000);

  REQUIRE(rex::kernel::xam::internal::TryConvertWsaTimeoutToNtWaitTimeout(0, &timeout));
  CHECK(timeout == 0);

  REQUIRE(rex::kernel::xam::internal::TryConvertWsaTimeoutToNtWaitTimeout(0xFFFFFFFEu, &timeout));
  CHECK(static_cast<int64_t>(timeout) == -42949672940000LL);

  constexpr uint64_t kSentinelTimeout = 0xAABBCCDDEEFF0011ull;
  timeout = kSentinelTimeout;
  CHECK_FALSE(rex::kernel::xam::internal::TryConvertWsaTimeoutToNtWaitTimeout(0xFFFFFFFFu,
                                                                              &timeout));
  CHECK(timeout == kSentinelTimeout);
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

TEST_CASE("XNetSetOpt updates startup parameters for XNetGetOpt", "[kernel][xam_net]") {
  constexpr u32 kCaller = 1;
  constexpr u32 kStartupParamsOption = 1;
  constexpr size_t kStartupParamsSize = 13;

  std::array<uint8_t, kStartupParamsSize> startup_params{
      {0x0D, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C}};
  std::array<uint8_t, kStartupParamsSize> output{};
  output.fill(0xCD);
  rex::be_u32 output_size = kStartupParamsSize;

  CHECK(rex::kernel::xam::NetDll_XNetSetOpt_entry(
            kCaller, kStartupParamsOption, mapped_void(startup_params.data(), 0x40005000),
            static_cast<u32>(startup_params.size())) == 0);
  CHECK(rex::kernel::xam::NetDll_XNetGetOpt_entry(
            kCaller, kStartupParamsOption, mapped_void(output.data(), 0x40006000),
            mapped_u32(&output_size, 0x40006010)) == 0);
  CHECK(output == startup_params);
  CHECK(u32(output_size) == kStartupParamsSize);

  CHECK(rex::kernel::xam::NetDll_XNetSetOpt_entry(
            kCaller, kStartupParamsOption, mapped_void(nullptr),
            static_cast<u32>(startup_params.size())) == kWsaEMsgSize);
  CHECK(rex::kernel::xam::NetDll_XNetSetOpt_entry(
            kCaller, kStartupParamsOption, mapped_void(startup_params.data(), 0x40005000),
            static_cast<u32>(startup_params.size() - 1)) == kWsaEMsgSize);
  CHECK(rex::kernel::xam::NetDll_XNetSetOpt_entry(
            kCaller, 0xDEAD, mapped_void(startup_params.data(), 0x40005000),
            static_cast<u32>(startup_params.size())) == kWsaEInval);
}

TEST_CASE("XNet address conversion helpers keep offline outputs deterministic",
          "[kernel][xam_net]") {
  std::array<uint8_t, 4> output{};
  output.fill(0xCD);

  CHECK(rex::kernel::xam::NetDll_XNetServerToInAddr_entry(
            1, mapped_void(nullptr), mapped_void(nullptr),
            mapped_void(output.data(), 0x40003000)) == 1);
  CHECK(ReadBe32(output.data(), 0) == 0);

  output.fill(0xCD);
  CHECK(rex::kernel::xam::NetDll_XNetInAddrToServer_entry(
            1, mapped_void(nullptr), mapped_void(nullptr),
            mapped_void(output.data(), 0x40003000)) == 1);
  CHECK(ReadBe32(output.data(), 0) == 0);

  output.fill(0xCD);
  CHECK(rex::kernel::xam::NetDll_XNetTsAddrToInAddr_entry(
            1, mapped_void(nullptr), mapped_void(nullptr),
            mapped_void(output.data(), 0x40003000)) == 1);
  CHECK(ReadBe32(output.data(), 0) == 0);
}

TEST_CASE("XNet status and listen-stat helpers report offline zero state",
          "[kernel][xam_net]") {
  constexpr u32 kStatus = 0x10;
  constexpr u32 kStats = 0x20;
  std::array<uint8_t, 0x60> storage{};
  WriteBe32(storage.data(), kStatus, 0xDEADBEEF);
  std::fill(storage.begin() + kStats, storage.begin() + kStats + 0x20, uint8_t{0xCD});

  CHECK(rex::kernel::xam::NetDll_XNetGetBroadcastVersionStatus_entry(
            1, mapped_u32(reinterpret_cast<rex::be_u32*>(storage.data() + kStatus),
                          kStatus)) == 0);
  CHECK(ReadBe32(storage.data(), kStatus) == 0);

  CHECK(rex::kernel::xam::NetDll_XNetQosGetListenStats_entry(
            1, mapped_void(nullptr), mapped_void(storage.data() + kStats, kStats),
            0x20) == 0);
  CHECK(std::all_of(storage.begin() + kStats, storage.begin() + kStats + 0x20,
                    [](uint8_t value) { return value == 0; }));
}

TEST_CASE("XNetQosLookup reports an empty offline result without kernel state",
          "[kernel][xam_net]") {
  rex::be_u32 qos_handle = 0xDEADBEEF;

  CHECK(rex::kernel::xam::NetDll_XNetQosLookup_entry(
            1, 0, mapped_void(nullptr), mapped_void(nullptr), mapped_void(nullptr), 0,
            mapped_void(nullptr), mapped_void(nullptr), 0, 0, 0, 0,
            mapped_u32(&qos_handle, 0x40004000)) == 0);
  CHECK(u32(qos_handle) == 0);
}

TEST_CASE("socket query helpers fail deterministically for missing sockets",
          "[kernel][xam_net]") {
  CHECK(rex::kernel::xam::NetDll_getsockopt_entry(
            1, 0xBAD, 0, 0, mapped_void(nullptr), mapped_u32(nullptr)) == kSocketError);
  CHECK(rex::kernel::xam::NetDll_getsockname_entry(
            1, 0xBAD, ppc_ptr_t<rex::system::XSOCKADDR>(nullptr),
            mapped_u32(nullptr)) == kSocketError);
  CHECK(rex::kernel::xam::NetDll_getpeername_entry(
            1, 0xBAD, ppc_ptr_t<rex::system::XSOCKADDR>(nullptr),
            mapped_u32(nullptr)) == kSocketError);
}

TEST_CASE("WSA socket operation helpers fail deterministically for missing sockets",
          "[kernel][xam_net]") {
  CHECK(rex::kernel::xam::NetDll_WSACancelOverlappedIO_entry(1, 0xBAD) == kSocketError);
  CHECK(rex::kernel::xam::NetDll_WSAEventSelect_entry(1, 0xBAD, 0, 0) == kSocketError);
  CHECK(rex::kernel::xam::NetDll_WSASend_entry(
            1, 0xBAD, ppc_ptr_t<rex::kernel::xam::XWSABUF>(nullptr), 0,
            mapped_u32(nullptr), 0, ppc_ptr_t<rex::kernel::xam::XWSAOVERLAPPED>(nullptr),
            mapped_void(nullptr)) == kSocketError);
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
