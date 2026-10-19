#pragma once
// SimPlayer: a headless client that speaks netproto frames to the sim's world/session boundary.
// Everything it does is a byte frame encoded by netproto::SimClient; everything it learns arrives as a frame
// decoded by the same client ("event", see sim_wire.hpp for the names). No sockets, no gateway.
#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "netproto/sim_client.hpp"
#include "tooling/sim_world.hpp"

namespace tooling::sim {

class SimPlayer {
 public:
  using Event = wire::Packet;

  SimPlayer(Sim& sim, int session, std::string name);

  const std::string& name() const { return name_; }
  uint32_t gid() const;
  uint32_t aid() const;
  int session() const { return session_; }
  bool connected() const;           // handshake finished and the session is still online
  bool disconnected() const;        // the server dropped the session
  std::string disconnect_reason() const;

  // ---- actions (each sends the exact frame a client would) ----------------------------------
  void connect();                   // CZ_ENTER, then CZ_NOTIFY_ACTORINIT after ZC_ACCEPT_ENTER
  void begin_connect();             // CZ_ENTER only (for connecting many players in the same ticks)
  void finish_connect();            // waits for the accept, sends CZ_NOTIFY_ACTORINIT, waits for the world
  static void connect_all(Sim& sim, const std::vector<SimPlayer*>& players);
  void walk(int x, int y);          // CZ_REQUEST_MOVE
  void say(const std::string& text);        // CZ_REQUEST_CHAT ("<name> : <text>")
  void click_npc(const std::string& npc_name);  // CZ_CONTACTNPC on the NPC's gid
  void click_npc_gid(uint32_t npc_gid);
  void answer(int choice);          // CZ_CHOOSE_MENU (1-based); 255 cancels, as the real client
  void next();                      // CZ_REQ_NEXT_SCRIPT (the Next/Close button)
  void input(const std::string& text);      // CZ_INPUT_EDITDLGSTR
  void input_number(int32_t value);         // CZ_INPUT_EDITDLG
  void close_dialog();              // CZ_CLOSE_DIALOG
  void ping();                      // CZ_REQUEST_TIME
  void attack(uint32_t target_gid); // CZ_REQUEST_ACT2, action 0
  void cast(const std::string& skill_id, uint32_t target_gid, uint16_t level = 1);  // CZ_USE_SKILL2
  void send_raw(const wire::Bytes& frame);  // any bytes (tests: out-of-phase packets, bad lengths)
  void disconnect();

  // ---- receiving -----------------------------------------------------------------------------------
  // Everything not yet consumed, oldest first (clears the inbox).
  std::vector<Event> drain_received();
  // First unconsumed event named `name` (a packet name or an alias, see alias()); advances the sim by
  // whole ticks up to `timeout_ms` of virtual time. The event is consumed. nullopt on timeout.
  std::optional<Event> wait_for(const std::string& name, int64_t timeout_ms);
  // Like wait_for but throws SimError naming what did arrive instead.
  Event expect(const std::string& name, int64_t timeout_ms);
  // True when no event `name` shows up within `within_ms` of virtual time.
  bool expect_none(const std::string& name, int64_t within_ms);
  std::optional<Event> last(const std::string& name) const;   // newest in history
  size_t count(const std::string& name) const;                 // in history
  const std::deque<Event>& history() const { return history_; }
  std::string describe_inbox() const;

  // ---- client-side knowledge ---------------------------------------------------------------------------
  bool sees(uint32_t gid) const { return seen_.count(gid) != 0; }
  const std::set<uint32_t>& visible() const { return seen_; }
  std::optional<engine::Point> position() const;   // authoritative position (server), for assertions
  std::optional<engine::Point> known_position() const { return known_pos_; }  // from the last frames received
  // Maps "say" -> ZC_SAY_DIALOG, "menu" -> ZC_MENU_LIST, "damage" -> ZC_NOTIFY_ACT, "standing" -> LEGACY_IDLE_UNIT, ... (see .cpp).
  static std::string alias(const std::string& name);

  // Called by Sim after every tick: decodes the server bytes into events.
  void pump();
  void set_history_cap(size_t n) { history_cap_ = n; }

 private:
  void feed_state(const Event& e);
  bool matches(const Event& e, const std::string& want) const;
  void send_frame(const wire::Bytes& f);
  void send_packet(const wire::DecodedPacket& p);  // encoded by the SimClient
  void send_packet(const std::string& name, wire::Fields fields);

  Sim& sim_;
  int session_;
  std::string name_;
  netproto::SimClient client_;   // the client half of the session: encode, phase tracking, server-frame decode
  wire::Bytes rx_;
  std::deque<Event> inbox_, history_;
  std::set<uint32_t> seen_;
  std::optional<engine::Point> known_pos_;
  bool accepted_ = false, refused_ = false;
  size_t history_cap_ = 50000;
};

}  // namespace tooling::sim
