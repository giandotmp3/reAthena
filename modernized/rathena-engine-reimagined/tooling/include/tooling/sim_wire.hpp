#pragma once
// The wire vocabulary rathena-sim speaks (client build re/20211103), at codec level: framed byte
// buffers, no sockets.
//
// Where each number comes from:
//   origin "spec"          the packet is in netproto/proto/client_packets.yaml (9 packets);
//                          opcode, length and gate are taken from the generated catalog.
//   origin "legacy-db"     opcode and length are the legacy packet DB's answer for this target
//                          (netproto/proto/legacy_20211103.yaml); the spec has no entry for it, and
//                          for handler-parsed packets no field layout either. The sim writes the
//                          fields at the offsets the legacy handler reads (the `pos` list) and
//                          documents the meaning; field TYPES are the sim's reading of
//                          src/map/clif.cpp, not netproto data.
//   origin "legacy-struct" a typed struct in the legacy DB (or packets_struct.hpp) that the spec
//                          does not list yet.
// tests/test_sim_wire.cpp cross-checks every opcode and length against legacy_20211103.yaml.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "netproto/codec_set.hpp"
#include "netproto/framing.hpp"
#include "netproto/pos_codec.hpp"

namespace tooling::sim::wire {

using netproto::ByteView;
using netproto::Bytes;

enum class Dir : uint8_t { C2S, S2C };

struct Def {
  uint16_t opcode;
  const char* name;
  int len;  // total bytes, or -1: variable, u16 length at offset 2
  Dir dir;
  netproto::Phase gate;
  const char* origin;
};
const std::vector<Def>& defs();
const Def* find(uint16_t opcode, Dir d);
const Def* find_name(std::string_view name);

// Decoded packet: a name plus integer / string fields (names below).
using FieldValue = std::variant<int64_t, std::string>;
using Fields = std::map<std::string, FieldValue>;
struct Packet {
  std::string name;
  uint16_t opcode = 0;
  Fields fields;
  Bytes raw;  // the whole frame
  int64_t at_ms = 0;
  int64_t num(const std::string& k, int64_t dflt = 0) const;
  std::string text(const std::string& k) const;
};

// Frame table for peek_frame on one direction (spec packets from `spec`, the rest from defs()).
netproto::FrameTable frame_table(Dir d, const netproto::CodecSet& spec);

// ---- little-endian helpers ---------------------------------------------------------------
void put16(Bytes& b, size_t at, uint32_t v);
void put32(Bytes& b, size_t at, uint32_t v);
uint32_t get16(ByteView b, size_t at);
uint32_t get32(ByteView b, size_t at);

// ---- client to server ----------------------------------------------------------------------
Bytes cz_enter(uint32_t aid, uint32_t cid, uint32_t login_id1, uint32_t tick, uint8_t sex);
Bytes cz_notify_actorinit();
Bytes cz_request_move(uint16_t x, uint16_t y, uint8_t dir);
Bytes cz_request_time(uint32_t tick);
Bytes cz_request_chat(std::string_view text);
Bytes cz_contactnpc(uint32_t npc_gid, uint8_t type);
Bytes cz_req_next_script(uint32_t npc_gid);
Bytes cz_choose_menu(uint32_t npc_gid, uint8_t choice);
Bytes cz_close_dialog(uint32_t npc_gid);
Bytes cz_input_editdlg(uint32_t npc_gid, int32_t value);
Bytes cz_input_editdlgstr(uint32_t npc_gid, std::string_view text);
Bytes cz_request_act(uint32_t target_gid, uint8_t action);
Bytes cz_use_skill(uint16_t level, uint16_t skill_id, uint32_t target_gid);

// ---- server to client ----------------------------------------------------------------------
struct Actor {
  uint32_t gid = 0;
  uint8_t object_type = 0;  // 0 player, 5 mob, 6 npc (clif_bl_type)
  uint16_t speed = 150;
  uint16_t job = 0;
  uint16_t level = 1;
  uint32_t max_hp = 1, hp = 1;
  uint8_t sex = 0;
  std::string name;
  uint16_t x = 0, y = 0, dir = 0;
  uint16_t to_x = 0, to_y = 0;  // walking packet only
  uint32_t move_start = 0;      // walking packet only
};
Bytes zc_accept_enter(uint32_t tick, uint16_t x, uint16_t y, uint8_t dir);
Bytes zc_refuse_enter(uint8_t code);
Bytes zc_notify_time(uint32_t tick);
Bytes zc_notify_playermove(uint32_t tick, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
Bytes zc_stopmove(uint32_t gid, uint16_t x, uint16_t y);
Bytes zc_notify_vanish(uint32_t gid, uint8_t type);
Bytes zc_notify_standentry(const Actor& a);   // 0x09ff, 108 bytes
Bytes zc_notify_moveentry(const Actor& a);    // 0x09fd, 114 bytes
Bytes zc_notify_chat(uint32_t gid, std::string_view msg);
Bytes zc_notify_playerchat(std::string_view msg);
Bytes zc_say_dialog(uint32_t npc_gid, std::string_view text);
Bytes zc_wait_dialog(uint32_t npc_gid);
Bytes zc_close_dialog(uint32_t npc_gid);
Bytes zc_menu_list(uint32_t npc_gid, const std::vector<std::string>& options);
Bytes zc_open_editdlgstr(uint32_t npc_gid);
Bytes zc_notify_act(uint32_t src, uint32_t dst, uint32_t tick, int32_t src_speed, int32_t dmg_speed, int32_t damage,
                    uint16_t div, uint8_t type, int32_t damage2);

// Decodes one complete frame (as delimited by peek_frame) of direction `d`. nullopt for an
// opcode the sim does not interpret (it is still a legal frame: callers keep the raw bytes).
std::optional<Packet> decode(ByteView frame, Dir d);

// Splits a byte stream into frames with the given table. `consumed` bytes were used; a
// malformed length or unknown opcode sets `error` (the session must be dropped).
struct Split {
  std::vector<Bytes> frames;
  size_t consumed = 0;
  std::string error;
};
Split split_frames(ByteView stream, const netproto::FrameTable& table);

}  // namespace tooling::sim::wire
