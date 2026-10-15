#pragma once
// BR-INTER-007: inter-server packet length tables.
//
// Ported as data from the legacy tables (K1: compile-time C++, not generated):
//   - chrif   0x2af8..0x2b34  map/chrif.cpp:42-50 packet_len_table
//   - intif   0x3000..0x30AF  char/inter.cpp:58-71 inter_recv_packet_length
//   - login<-char 0x27xx      login/loginchrif.cpp handler size checks
//
// HITL #1 drop: the auction family 0x3050-0x3055 is removed from the intif
// table, so those opcodes are unknown and disconnect the link (BR-INTER-002).

#include "rathena/netproto/frame.hpp"

namespace rathena::netproto::inter {

/// Packets the login-server accepts from a registered char-server.
[[nodiscard]] const PacketTable& login_from_char_table();

/// chrif family (shared table, both directions as in legacy).
[[nodiscard]] const PacketTable& chrif_table();

/// intif family map -> char (auction dropped).
[[nodiscard]] const PacketTable& intif_map_to_char_table();

/// Full post-registration map -> char link: chrif + intif.
[[nodiscard]] const PacketTable& map_to_char_link_table();

}  // namespace rathena::netproto::inter
