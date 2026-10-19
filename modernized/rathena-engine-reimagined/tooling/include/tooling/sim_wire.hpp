#pragma once
// The wire vocabulary rathena-sim speaks (client build re/20211103), at codec level: framed byte buffers, no
// sockets. There is NO codec in this file: every frame is encoded and decoded by netproto's CodecSet over the full
// generated catalog of the real client (1,186 packets), names and field names are the catalog's (the legacy
// struct members: AID, NpcID, PosDir, ...; `LEGACY_IDLE_UNIT` / `LEGACY_UNIT_WALKING` for 0x09ff / 0x09fd,
// `CZ_USE_SKILL2` for 0x0438, `OPAQUE_008E` for 0x008e, handler-derived members f2/f4/f6 where the legacy handler
// has no variable). The client side of a session is netproto::SimClient (see sim_player.hpp).
//
// What is left here is glue: the shared codec set, the event type SimPlayer hands to tests and scenarios, and
// tiny builders (`frame`, `chat_tail`) that cut the ceremony of filling a DecodedPacket.
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "netproto/codec_set.hpp"
#include "netproto/framing.hpp"
#include "netproto/pos_codec.hpp"

namespace tooling::sim::wire {

using netproto::Bytes;
using netproto::ByteView;
using netproto::DecodedPacket;
using netproto::FieldValue;

/// The one codec set of the sim (re/20211103, full catalog), built once per process; immutable.
const std::shared_ptr<const netproto::CodecSet>& codecs();

/// A packet the sim saw: netproto's decoded packet (`name`, `fields`, `repeats`) plus the frame it came from.
struct Packet : netproto::DecodedPacket {
  uint16_t opcode = 0;
  Bytes raw;  // the whole frame
  int64_t at_ms = 0;
  /// Integer field `k` (netproto's name), `dflt` when absent or not an integer.
  int64_t num(const std::string& k, int64_t dflt = 0) const;
  /// Text of a string field, or of a raw tail / payload up to its first NUL; empty when absent.
  std::string text(const std::string& k) const;
  netproto::Pos3 pos3(const std::string& k) const;  // zeros when absent
  netproto::Pos6 pos6(const std::string& k) const;
};
Packet make_packet(netproto::DecodedPacket p, ByteView frame, uint16_t opcode);

using Fields = std::initializer_list<std::pair<const char*, FieldValue>>;
inline FieldValue I(int64_t v) { return v; }

/// Encodes packet `name` with these values through netproto's codec. Throws netproto::CodecError.
Bytes frame(const std::string& name, Fields fields);
/// CZ_REQUEST_CHAT's `tail` is a raw tail in the catalog (handler-derived): the text plus its NUL.
Bytes chat_tail(const std::string& text);

/// Splits a byte stream into frames with `table`. `consumed` bytes were used; a malformed length or unknown
/// opcode sets `error` (the session must be dropped).
struct Split {
  std::vector<Bytes> frames;
  size_t consumed = 0;
  std::string error;
};
Split split_frames(ByteView stream, const netproto::FrameTable& table);

// A unit as the two entry packets describe it (the sim's reading of its own world, not a layout).
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
/// LEGACY_IDLE_UNIT (0x09ff, 108 bytes) when !walking, LEGACY_UNIT_WALKING (0x09fd, 114 bytes) otherwise.
Bytes actor_frame(const Actor& a, bool walking);

}  // namespace tooling::sim::wire
