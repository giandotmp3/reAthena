#include "rathena/netproto/inter/length_tables.hpp"

#include <array>
#include <cstdint>

#include "rathena/netproto/inter/version.hpp"

namespace rathena::netproto::inter {

namespace {

// common/mmo.hpp widths used inside the legacy tables.
constexpr std::int32_t NAME_LENGTH = static_cast<std::int32_t>(kNameLength);  // 24
constexpr std::int32_t MAP_NAME_LENGTH_EXT = 12 + 4;                          // 16
constexpr std::int32_t ACHIEVEMENT_NAME_LENGTH = 50;

// map/chrif.cpp:42-50 (0x2af8..0x2b34; trailing entries are 0 in legacy).
constexpr std::array<std::int32_t, 0x3d> kChrifLengths = {
    60, 3, -1, -1, 10, -1, 6, -1,                        // 2af8-2aff
    6, -1, 18, 7, -1, -1, 28 + MAP_NAME_LENGTH_EXT, 10,  // 2b00-2b07
    6, 30, 10, -1, 86, 7, 44, 34,                        // 2b08-2b0f
    11, 10, 10, 0, 11, -1, 0, 10,                        // 2b10-2b17
    2, 10, 2, -1, -1, -1, 2, 7,                          // 2b18-2b1f
    -1, 10, 8, 2, 2, 14, 19, 19,                         // 2b20-2b27
    -1, 0, 6, 15, 0, 6, -1, -1,                          // 2b28-2b2f
};

// char/inter.cpp:58-71 (0x3000..0x30AF). Auction 0x3050-0x3055 zeroed: dropped at HITL #1.
constexpr std::array<std::int32_t, 0xB0> kIntifLengths = {
    -1, -1, 7, -1, -1, 13, 36, (2 + 4 + 4 + 4 + NAME_LENGTH), 0, -1, 0, 0, 0, 0, 0, 0,  // 3000-
    6, -1, 0, 0, 0, 0, 0, 0, 10, -1, 0, 0, 0, 0, 0, 0,                                  // 3010-
    -1, 10, -1, 14, 15 + NAME_LENGTH, 17 + MAP_NAME_LENGTH_EXT, 6, -1, 14, 14, 6, 0, 0, 0, 0, 0,  // 3020- party
    -1, 6, -1, -1, 55, 19, 6, -1, 14, -1, -1, -1, 18, 19, 186, -1,                      // 3030- guild
    -1, 9, 10, 0, 0, 0, 0, 0, 8, 6, 11, 10, 10, -1, 6 + NAME_LENGTH, 0,                 // 3040- mail
    0, 0, 0, 0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                    // 3050- (auction dropped)
    6, -1, 6, -1, 16 + NAME_LENGTH + ACHIEVEMENT_NAME_LENGTH, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 3060- quest/achv
    -1, 10, 6, -1, 0, 0, 0, 0, 0, 0, 0, 0, -1, 10, 6, -1,                               // 3070- merc/elemental
    52, 14, -1, 6, 0, 0, 0, 0, 0, 0, 13, -1, 0, 0, 0, 0,                                // 3080- pet/storage
    -1, 10, -1, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                  // 3090- homunculus
    2, -1, 6, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                                    // 30A0- clan
};

PacketTable build_login_from_char() {
  PacketTable t("login<-char", FrameLimits::server_link());
  t.add(PacketSpec::fixed(0x2712, 23))   // auth verify
      .add(PacketSpec::fixed(0x2714, 6))     // user count
      .add(PacketSpec::fixed(0x2716, 6))     // account data request
      .add(PacketSpec::fixed(0x2719, 2))     // keepalive
      .add(PacketSpec::fixed(0x2720, 18))    // @accinfo
      .add(PacketSpec::fixed(0x2722, 86))    // e-mail change
      .add(PacketSpec::fixed(0x2724, 10))    // state change
      .add(PacketSpec::fixed(0x2725, 10))    // ban
      .add(PacketSpec::fixed(0x2727, 6))     // sex change
      .add(PacketSpec::variable(0x2728, 12)) // ## registry save: reads aid@4, char_id@8 (S4 header)
      .add(PacketSpec::fixed(0x272a, 6))     // unban
      .add(PacketSpec::fixed(0x272b, 6))     // set online
      .add(PacketSpec::fixed(0x272c, 6))     // set offline
      .add(PacketSpec::variable(0x272d, 6))  // online list: reads users@4 (S4 header)
      .add(PacketSpec::fixed(0x272e, 10))    // ## registry request
      .add(PacketSpec::fixed(0x2736, 6))     // char ip update
      .add(PacketSpec::fixed(0x2737, 2))     // set all offline
      .add(PacketSpec::fixed(0x2738, 13))    // pincode update (8 + PINCODE_LENGTH + 1)
      .add(PacketSpec::fixed(0x2739, 6))     // pincode auth fail
      .add(PacketSpec::fixed(0x2742, 15));   // VIP data request
  return t;
}

}  // namespace

const PacketTable& login_from_char_table() {
  static const PacketTable t = build_login_from_char();
  return t;
}

const PacketTable& chrif_table() {
  static const PacketTable t =
      PacketTable::from_legacy_lengths("chrif", FrameLimits::server_link(), 0x2af8, kChrifLengths);
  return t;
}

const PacketTable& intif_map_to_char_table() {
  static const PacketTable t =
      PacketTable::from_legacy_lengths("intif map->char", FrameLimits::server_link(), 0x3000, kIntifLengths);
  return t;
}

const PacketTable& map_to_char_link_table() {
  static const PacketTable t = [] {
    PacketTable merged("char<-map", FrameLimits::server_link());
    merged.merge(chrif_table()).merge(intif_map_to_char_table());
    return merged;
  }();
  return t;
}

}  // namespace rathena::netproto::inter
