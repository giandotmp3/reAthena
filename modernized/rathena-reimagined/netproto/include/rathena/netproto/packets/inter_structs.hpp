#pragma once
// Representative compile-time packet layouts (K1: packet layouts stay
// compile-time C++; the full client headers are carried over verbatim when
// netproto moves into src/). These structs document the wire layout and are
// size-checked; the codecs use explicit byte-order helpers instead of
// reinterpret_cast so the code is alignment- and endian-safe on every toolchain.

#include <cstdint>

#include "rathena/netproto/inter/version.hpp"

namespace rathena::netproto::packets {

#if defined(_MSC_VER)
#pragma pack(push, 1)
#define RATHENA_NETPROTO_PACKED
#else
#pragma pack(push, 1)
#define RATHENA_NETPROTO_PACKED __attribute__((packed))
#endif

struct RATHENA_NETPROTO_PACKED PACKET_CHAR_SERVER_LOGIN {  // 0x2710
  std::uint16_t packetType;
  char userid[inter::kNameLength];
  char passwd[inter::kNameLength];
  std::uint32_t protoVersion;  // legacy: always 0
  std::uint32_t ip;            // network byte order
  std::uint16_t port;          // network byte order
  char name[inter::kServerNameLength];
  std::uint16_t reserved;
  std::uint16_t type;
  std::uint16_t newDisplay;
};
static_assert(sizeof(PACKET_CHAR_SERVER_LOGIN) == 86, "0x2710 layout");

struct RATHENA_NETPROTO_PACKED PACKET_MAP_SERVER_LOGIN {  // 0x2af8
  std::uint16_t packetType;
  char userid[inter::kNameLength];
  char passwd[inter::kNameLength];
  std::uint32_t protoVersion;
  std::uint32_t ip;
  std::uint16_t port;
};
static_assert(sizeof(PACKET_MAP_SERVER_LOGIN) == 60, "0x2af8 layout");

struct RATHENA_NETPROTO_PACKED PACKET_SERVER_LOGIN_ACK {  // 0x2711 / 0x2af9
  std::uint16_t packetType;
  std::uint8_t result;
};
static_assert(sizeof(PACKET_SERVER_LOGIN_ACK) == 3, "0x2711/0x2af9 layout");

struct RATHENA_NETPROTO_PACKED PACKET_ACCOUNT_DATA_V0 {  // 0x2717, legacy
  std::uint16_t packetType;
  std::uint32_t aid;
  char email[inter::kEmailLength];
  std::uint32_t expirationTime;
  std::uint8_t groupId;
  std::uint8_t charSlots;
  char birthdate[inter::kBirthdateLength];
  char pincode[inter::kPincodeLength];
  std::uint32_t pincodeChange;
  std::uint8_t isVip;
  std::uint8_t charVip;
  std::uint8_t charBilling;
};
static_assert(sizeof(PACKET_ACCOUNT_DATA_V0) == 75, "0x2717 v0 layout");

struct RATHENA_NETPROTO_PACKED PACKET_ACCOUNT_DATA_V1 {  // 0x2717, widened group_id
  PACKET_ACCOUNT_DATA_V0 v0;
  std::int32_t groupIdWide;
};
static_assert(sizeof(PACKET_ACCOUNT_DATA_V1) == 79, "0x2717 v1 layout");

#pragma pack(pop)
#undef RATHENA_NETPROTO_PACKED

}  // namespace rathena::netproto::packets
