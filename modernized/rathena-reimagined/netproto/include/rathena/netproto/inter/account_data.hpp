#pragma once
// Account-data packet login -> char: 0x2717 (answer to 0x2716).
//
// BR-INTER-003 [DEFECT fix]: legacy sends group_id as 1 byte, truncating
// groups above 255 (loginchrif.cpp:194). The rebuild widens it.
//
// v0 (legacy, 75 B):
//   0 cmd u16 | 2 aid u32 | 6 email[40] | 46 expiration u32 | 50 group u8
//   | 51 char_slots u8 | 52 birthdate[11] | 63 pincode[5] | 68 pincode_change u32
//   | 72 is_vip u8 | 73 char_vip u8 | 74 char_billing u8
// v1 (79 B): v0 layout + 75 group_id i32 (LE). Offset 50 carries the group
//   saturated to 0..255 so a v0 reader never sees a silently wrapped value.
//
// Encoding a group_id outside 0..255 for a v0 peer is an error
// (kNotRepresentable) rather than a silent truncation.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "rathena/netproto/byte_order.hpp"
#include "rathena/netproto/codec_result.hpp"
#include "rathena/netproto/frame.hpp"
#include "rathena/netproto/inter/version.hpp"

namespace rathena::netproto::inter {

inline constexpr std::uint16_t kOpAccountDataRequest = 0x2716;
inline constexpr std::uint16_t kOpAccountData = 0x2717;
inline constexpr std::size_t kAccountDataSizeV0 = 75;
inline constexpr std::size_t kAccountDataSizeV1 = 79;

struct AccountData {
  std::uint32_t account_id = 0;
  std::string email;            // <= 39 chars
  std::uint32_t expiration_time = 0;
  std::int32_t group_id = 0;    // widened (BR-INTER-003)
  std::uint8_t char_slots = 0;
  std::string birthdate;        // <= 10 chars, "YYYY-MM-DD"
  std::string pincode;          // <= 4 chars
  std::uint32_t pincode_change = 0;
  bool is_vip = false;
  std::uint8_t char_vip = 0;
  std::uint8_t char_billing = 0;

  friend bool operator==(const AccountData&, const AccountData&) = default;
};

[[nodiscard]] CodecResult<std::size_t> account_data_size(ProtocolVersion version) noexcept;
[[nodiscard]] CodecResult<PacketSpec> account_data_spec(ProtocolVersion version) noexcept;

[[nodiscard]] CodecResult<std::vector<std::uint8_t>> encode(const AccountData& p, ProtocolVersion version);
[[nodiscard]] CodecResult<AccountData> decode_account_data(ByteView packet, ProtocolVersion version);

}  // namespace rathena::netproto::inter
