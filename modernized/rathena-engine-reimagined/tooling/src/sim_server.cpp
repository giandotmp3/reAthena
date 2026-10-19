// The sim's world/session boundary: terminates netproto frames from SimPlayers, drives engine-core
// (movement, grid, spatial index) and script-host (dialogue, bus), and writes frames back.
// This is NOT the gateway: no login server, no tickets, no sockets, no obfuscation, no flood guard.
#include <algorithm>
#include <cstdio>
#include <iterator>

#include "netproto/connect_check.hpp"
#include "netproto/framing.hpp"
#include "tooling/sim_player.hpp"
#include "tooling/sim_world.hpp"

namespace tooling::sim {

namespace {
uint64_t ent_key(engine::EntityId e) { return (uint64_t(e.generation) << 32) | e.index; }

std::string lq(const std::string& s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
    else if (c == '\n') o += "\\n";
    else if (c < 0x20 || c == 0x7F) { char b[8]; std::snprintf(b, sizeof b, "\\%03d", c); o += b; }
    else o += static_cast<char>(c);
  }
  return o + "\"";
}

uint64_t fnv_bytes(const wire::Bytes& b) {
  uint64_t h = 1469598103934665603ull;
  for (uint8_t c : b) { h ^= c; h *= 1099511628211ull; }
  return h;
}
}  // namespace

// ---- units --------------------------------------------------------------------------------------------
Sim::Unit* Sim::unit_of_gid(uint32_t gid) {
  auto it = units_.find(gid);
  return it == units_.end() ? nullptr : &it->second;
}
const Sim::Unit* Sim::unit_of_gid(uint32_t gid) const {
  auto it = units_.find(gid);
  return it == units_.end() ? nullptr : &it->second;
}
Sim::Session* Sim::session_of_gid(uint32_t gid) {
  Unit* u = unit_of_gid(gid);
  if (!u || u->session < 0) return nullptr;
  return sessions_[static_cast<size_t>(u->session)].get();
}

Sim::Unit& Sim::make_unit(uint8_t kind, const std::string& name, int x, int y) {
  if (!grid_->in_bounds(x, y)) throw SimError("position (" + std::to_string(x) + "," + std::to_string(y) + ") is outside the map");
  const uint32_t gid = next_gid_++;
  engine::EntityId e = reg_.create();
  reg_.emplace<engine::Position>(e, engine::Position{x, y});
  reg_.emplace<engine::Facing>(e);
  reg_.emplace<engine::Mover>(e, engine::Mover{cfg_.walk_step_ms, false});
  Unit u;
  u.gid = gid;
  u.kind = kind;
  u.name = name;
  u.ent = e;
  gid_of_ent_[ent_key(e)] = gid;
  return units_.emplace(gid, std::move(u)).first->second;
}

std::optional<engine::Point> Sim::position_of(uint32_t gid) const {
  const Unit* u = unit_of_gid(gid);
  if (!u) return std::nullopt;
  auto* p = const_cast<engine::Registry&>(reg_).get<engine::Position>(u->ent);
  if (!p) return std::nullopt;
  return engine::Point{p->x, p->y};
}

SimPlayer* Sim::player_by_gid(uint32_t gid) {
  for (auto& p : players_)
    if (p->gid() == gid) return p.get();
  return nullptr;
}

uint32_t Sim::spawn_dummy(const std::string& name, int x, int y, const std::string& unit_src) {
  Unit& u = make_unit(2, name, x, y);
  u.unit_src = unit_src;
  spatial_->insert(u.ent, {x, y}, engine::BucketClass::Primary);
  update_view(u.gid);
  trace("spawn dummy " + name + " gid=" + std::to_string(u.gid));
  return u.gid;
}

void Sim::set_unit(uint32_t gid, const std::string& unit_src) {
  Unit* u = unit_of_gid(gid);
  if (!u) throw SimError("unknown unit " + std::to_string(gid));
  u->unit_src = unit_src;
}

uint32_t Sim::place_npc(const std::string& name, int x, int y) {
  if (!npc_index_.count(name)) throw SimError("NPC '" + name + "' is not declared by any loaded file");
  if (npc_by_name_.count(name)) throw SimError("NPC '" + name + "' is already placed");
  Unit& u = make_unit(1, name, x, y);
  spatial_->insert(u.ent, {x, y}, engine::BucketClass::Secondary);
  npc_by_name_[name] = u.gid;
  update_view(u.gid);
  trace("place npc " + name + " gid=" + std::to_string(u.gid));
  return u.gid;
}

uint32_t Sim::npc_gid(const std::string& name) const {
  auto it = npc_by_name_.find(name);
  if (it == npc_by_name_.end()) throw SimError("NPC '" + name + "' is not placed");
  return it->second;
}

// ---- players ---------------------------------------------------------------------------------------------
SimPlayer& Sim::add_player(const PlayerSpec& spec) {
  auto s = std::make_unique<Session>();
  s->idx = static_cast<int>(sessions_.size());
  s->aid = spec.aid ? spec.aid : next_aid_++;
  s->name = spec.name.empty() ? "player" + std::to_string(s->idx) : spec.name;
  s->spec = spec;
  s->spec.name = s->name;
  sessions_.push_back(std::move(s));
  players_.push_back(std::make_unique<SimPlayer>(*this, sessions_.back()->idx, sessions_.back()->name));
  SimPlayer& p = *players_.back();
  if (spec.connect) p.connect();
  return p;
}

void Sim::client_send(int session, wire::ByteView bytes) {
  Session& s = *sessions_.at(static_cast<size_t>(session));
  s.in.insert(s.in.end(), bytes.begin(), bytes.end());
}
wire::Bytes Sim::client_take(int session) {
  Session& s = *sessions_.at(static_cast<size_t>(session));
  wire::Bytes out;
  out.swap(s.out);
  return out;
}
void Sim::client_close(int session) {
  Session& s = *sessions_.at(static_cast<size_t>(session));
  drop_session(s, "client closed the connection");
}

void Sim::send(Session& s, const wire::Bytes& frame) {
  if (s.idx < 0) return;
  char buf[64];
  std::snprintf(buf, sizeof buf, "out s=%d %04x/%zu h=%016llx", s.idx, frame.size() >= 2 ? static_cast<unsigned>(frame[0] | frame[1] << 8) : 0u, frame.size(),
                static_cast<unsigned long long>(fnv_bytes(frame)));
  trace(buf);
  s.out.insert(s.out.end(), frame.begin(), frame.end());
}

void Sim::drop_session(Session& s, const std::string& why) {
  trace("drop s=" + std::to_string(s.idx) + " " + why);
  if (s.online || s.ent.valid()) {
    if (s.dlg_active) {
      dialogues_->on_disconnect(s.gid);
      tell_runtime_dialogue_ended(s);
      s.dlg_active = false;
    }
    if (Unit* u = unit_of_gid(s.gid)) {
      // persist the last position (the sim's own storage namespace)
      if (auto pos = position_of(s.gid)) {
        auto t = std::make_shared<script_host::ValueTable>();
        t->entries["x"] = static_cast<int64_t>(pos->x);
        t->entries["y"] = static_cast<int64_t>(pos->y);
        try {
          host_->storage().set(script_host::Scope::Character, std::to_string(s.aid), "sim.world", "pos", script_host::Value(t));
        } catch (const std::exception& e) {
          trace(std::string("persist failed: ") + e.what());
        }
      }
      movement_->stop(u->ent);
      std::vector<uint32_t> watchers(u->view.begin(), u->view.end());
      std::sort(watchers.begin(), watchers.end());
      for (uint32_t g : watchers) {
        if (Unit* v = unit_of_gid(g)) {
          v->view.erase(s.gid);
          if (Session* vs = session_of_gid(g)) send(*vs, "ZC_NOTIFY_VANISH", {{"gid", wire::I(s.gid)}, {"type", wire::I(3)}});  // 3: logged out
        }
      }
      spatial_->remove(u->ent);
      walkers_.erase(std::remove(walkers_.begin(), walkers_.end(), s.gid), walkers_.end());
      gid_of_ent_.erase(ent_key(u->ent));
      reg_.destroy(u->ent);
      units_.erase(s.gid);
    }
  }
  s.online = false;
  s.ent = {};
  s.phase = netproto::Phase::PreSession;
  s.in.clear();
  s.disconnect_reason = why;
}

// ---- inbound -----------------------------------------------------------------------------------------------
void Sim::process_inbound(Session& s) {
  while (!s.in.empty()) {
    auto r = netproto::peek_frame(s.in, c2s_);
    if (r.status == netproto::FrameStatus::kNeedMore) break;
    if (r.status != netproto::FrameStatus::kComplete) {
      drop_session(s, r.status == netproto::FrameStatus::kUnknownOpcode ? "unknown opcode" : "invalid declared length");
      return;
    }
    if (!s.budget.try_consume()) break;  // BR-MAP-059: three packets per socket per cycle, the rest waits
    wire::Bytes frame(s.in.begin(), s.in.begin() + r.length);
    s.in.erase(s.in.begin(), s.in.begin() + r.length);
    char buf[64];
    std::snprintf(buf, sizeof buf, "in s=%d %04x/%zu h=%016llx", s.idx, r.opcode, frame.size(), static_cast<unsigned long long>(fnv_bytes(frame)));
    trace(buf);
    const auto& entry = c2s_.at(r.opcode);
    if (netproto::gate_packet(s.phase, entry.gate) == netproto::GateDecision::kDisconnect) {
      drop_session(s, "packet " + entry.name + " is not valid in this session phase");
      return;
    }
    if (!handled_packet(entry.name)) { trace("in s=" + std::to_string(s.idx) + " " + entry.name + " not interpreted by the sim"); continue; }
    netproto::DecodedPacket decoded;
    if (const netproto::CodecErrc e = codec_->try_decode(frame, netproto::Direction::ClientToServer, decoded); e != netproto::CodecErrc::kOk) {
      drop_session(s, std::string("malformed ") + entry.name + ": " + netproto::to_string(e));
      return;
    }
    handle_frame(s, wire::make_packet(std::move(decoded), frame, r.opcode));
  }
}

// The client packets the sim answers (names are netproto's). Any other packet of the catalog is legal on the wire,
// is gated and counted like the rest, and then ignored.
bool Sim::handled_packet(const std::string& name) {
  static const std::set<std::string> names = {"CZ_ENTER", "CZ_NOTIFY_ACTORINIT", "CZ_REQUEST_MOVE", "CZ_REQUEST_CHAT", "CZ_CONTACTNPC",
                                              "CZ_REQUEST_TIME", "CZ_REQ_NEXT_SCRIPT", "CZ_CHOOSE_MENU", "CZ_CLOSE_DIALOG",
                                              "CZ_INPUT_EDITDLG", "CZ_INPUT_EDITDLGSTR", "CZ_REQUEST_ACT2", "CZ_USE_SKILL2"};
  return names.count(name) != 0;
}

void Sim::handle_frame(Session& s, const wire::Packet& p) {
  if (p.name == "CZ_ENTER") return on_connect(s, p);
  if (p.name == "CZ_NOTIFY_ACTORINIT") return on_actorinit(s);
  if (p.name == "CZ_REQUEST_MOVE") return on_walk(s, p);
  if (p.name == "CZ_REQUEST_CHAT") return on_chat(s, p);
  if (p.name == "CZ_CONTACTNPC") return on_click_npc(s, p);
  if (p.name == "CZ_REQUEST_TIME") return send(s, "ZC_NOTIFY_TIME", {{"time", wire::I(server_tick())}});
  if (p.name == "CZ_REQ_NEXT_SCRIPT" || p.name == "CZ_CHOOSE_MENU" || p.name == "CZ_CLOSE_DIALOG" || p.name == "CZ_INPUT_EDITDLG" ||
      p.name == "CZ_INPUT_EDITDLGSTR")
    return on_dialogue_reply(s, p);
  if (p.name == "CZ_REQUEST_ACT2") return on_act(s, p);
  if (p.name == "CZ_USE_SKILL2") return on_skill(s, p);
}

void Sim::on_connect(Session& s, const wire::Packet& p) {
  const auto parsed = netproto::connect_packet_from(p);  // the catalog's CZ_ENTER members, nothing hard-coded here
  if (!parsed) { drop_session(s, "connect refused"); return; }
  const netproto::ConnectPacket cp = *parsed;
  bool already = false;
  for (auto& o : sessions_)
    if (o->online && o->aid == static_cast<uint32_t>(cp.account_id) && o->idx != s.idx) already = true;
  const auto expected = static_cast<size_t>(c2s_.at(p.opcode).length.fixed_size);
  auto err = netproto::validate_connect(p.raw.size(), expected, cp, already);
  if (err != netproto::ConnectError::kNone) {
    if (err != netproto::ConnectError::kAlreadyLoggedIn) send(s, "ZC_REFUSE_ENTER", {{"errorCode", wire::I(netproto::refusal_code(err))}});
    drop_session(s, "connect refused");
    return;
  }
  s.aid = static_cast<uint32_t>(cp.account_id);
  int x = s.spec.x, y = s.spec.y;
  // restore the last saved position, if any (persistence round trip)
  try {
    auto v = host_->storage().get(script_host::Scope::Character, std::to_string(s.aid), "sim.world", "sim.world", "pos");
    if (v) {
      if (auto* t = std::get_if<std::shared_ptr<script_host::ValueTable>>(&*v)) {
        auto xi = (*t)->entries.find("x"), yi = (*t)->entries.find("y");
        if (xi != (*t)->entries.end() && yi != (*t)->entries.end()) {
          x = static_cast<int>(std::get<int64_t>(xi->second));
          y = static_cast<int>(std::get<int64_t>(yi->second));
        }
      }
    }
  } catch (const std::exception& e) {
    trace(std::string("restore failed: ") + e.what());
  }
  Unit& u = make_unit(0, s.name, x, y);
  u.session = s.idx;
  u.job = 0;
  u.level = static_cast<uint16_t>(s.spec.level);
  u.unit_src = s.spec.unit;
  s.ent = u.ent;
  s.gid = u.gid;
  s.online = true;
  s.phase = netproto::Phase::PreMapLoad;
  trace("connect s=" + std::to_string(s.idx) + " aid=" + std::to_string(s.aid) + " gid=" + std::to_string(s.gid));
  send(s, "ZC_ACCEPT_ENTER", {{"startTime", wire::I(server_tick())}, {"posDir", netproto::Pos3{static_cast<uint16_t>(x), static_cast<uint16_t>(y), 0}},
                               {"xSize", wire::I(5)}, {"ySize", wire::I(5)}, {"font", wire::I(0)}});
}

void Sim::on_actorinit(Session& s) {
  Unit* u = unit_of_gid(s.gid);
  if (!u) return;
  s.phase = netproto::Phase::InWorld;
  auto pos = position_of(s.gid);
  spatial_->insert(u->ent, *pos, engine::BucketClass::Primary);
  update_view(s.gid);
  if (bus_has_hooks("sim:enter")) dispatch("sim:enter", s.gid);
}

void Sim::on_walk(Session& s, const wire::Packet& p) {
  Unit* u = unit_of_gid(s.gid);
  if (!u) return;
  const netproto::Pos3 dest = p.pos3("pos");
  const int x = dest.x, y = dest.y;
  auto pos = position_of(s.gid);
  if (!grid_->in_bounds(x, y) || !movement_->request_walk(u->ent, {x, y}).accepted) {
    send(s, "ZC_STOPMOVE", {{"AID", wire::I(s.gid)}, {"xPos", wire::I(pos->x)}, {"yPos", wire::I(pos->y)}});  // legacy: clif_fixpos
    return;
  }
  if (!movement_->walking(u->ent)) return;  // already there
  u->dest = {x, y};
  u->walking = true;
  if (std::find(walkers_.begin(), walkers_.end(), s.gid) == walkers_.end()) walkers_.push_back(s.gid);
  send(s, "ZC_NOTIFY_PLAYERMOVE",
       {{"moveStartTime", wire::I(server_tick())},
        {"moveData", netproto::Pos6{static_cast<uint16_t>(pos->x), static_cast<uint16_t>(pos->y), static_cast<uint16_t>(x), static_cast<uint16_t>(y), 8, 8}}});
  std::vector<uint32_t> watchers(u->view.begin(), u->view.end());
  std::sort(watchers.begin(), watchers.end());
  wire::Bytes f = wire::actor_frame(actor_of(*u, true), true);
  for (uint32_t g : watchers)
    if (Session* vs = session_of_gid(g)) send(*vs, f);
}

void Sim::on_chat(Session& s, const wire::Packet& p) {
  Unit* u = unit_of_gid(s.gid);
  if (!u) return;
  const std::string text = p.text("tail");
  const std::string prefix = s.name + " : ";
  if (text.rfind(prefix, 0) != 0) {  // legacy clif_parse_GlobalMessage: a message not prefixed with the speaker is dropped
    trace("chat dropped (not prefixed with the speaker) s=" + std::to_string(s.idx));
    return;
  }
  if (bus_has_hooks("sim:chat")) {
    HookResult r = dispatch("sim:chat", s.gid);
    if (r.cancelled) return;
  }
  send(s, "OPAQUE_008E", {{"payload", wire::chat_tail(text)}});  // 0x008e: length-only in the legacy tables
  std::vector<uint32_t> watchers(u->view.begin(), u->view.end());
  std::sort(watchers.begin(), watchers.end());
  wire::Bytes f = wire::frame("ZC_NOTIFY_CHAT", {{"GID", wire::I(s.gid)}, {"Message", text}});
  for (uint32_t g : watchers)
    if (Session* vs = session_of_gid(g)) send(*vs, f);
}

// ---- views --------------------------------------------------------------------------------------------------
wire::Actor Sim::actor_of(const Unit& u, bool walking_form) const {
  wire::Actor a;
  a.gid = u.gid;
  a.object_type = u.kind == 0 ? 0 : (u.kind == 1 ? 6 : 5);
  a.speed = static_cast<uint16_t>(cfg_.walk_step_ms);
  a.level = u.level;
  a.max_hp = u.max_hp;
  a.hp = u.hp;
  a.name = u.name;
  auto pos = position_of(u.gid);
  a.x = static_cast<uint16_t>(pos ? pos->x : 0);
  a.y = static_cast<uint16_t>(pos ? pos->y : 0);
  if (walking_form) {
    a.to_x = static_cast<uint16_t>(u.dest.x);
    a.to_y = static_cast<uint16_t>(u.dest.y);
    a.move_start = server_tick();
  }
  return a;
}

void Sim::send_appear(Session& to, const Unit& who) {
  if (to.phase != netproto::Phase::InWorld) return;
  send(to, wire::actor_frame(actor_of(who, who.walking), who.walking));
}

void Sim::update_view(uint32_t gid) {
  Unit* u = unit_of_gid(gid);
  if (!u) return;
  auto pos = spatial_->position_of(u->ent);
  if (!pos) return;
  std::vector<uint32_t> now;
  spatial_->for_each_in_area(engine::AreaQuery::around(*pos, cfg_.view_range), [&](engine::EntityId e) {
    auto it = gid_of_ent_.find(ent_key(e));
    if (it != gid_of_ent_.end() && it->second != gid) now.push_back(it->second);
  });
  std::sort(now.begin(), now.end());
  now.erase(std::unique(now.begin(), now.end()), now.end());
  std::vector<uint32_t> old(u->view.begin(), u->view.end());
  std::sort(old.begin(), old.end());
  std::vector<uint32_t> gained, lost;
  std::set_difference(now.begin(), now.end(), old.begin(), old.end(), std::back_inserter(gained));
  std::set_difference(old.begin(), old.end(), now.begin(), now.end(), std::back_inserter(lost));
  Session* me = session_of_gid(gid);
  for (uint32_t g : gained) {
    Unit* v = unit_of_gid(g);
    if (!v) continue;
    u->view.insert(g);
    v->view.insert(gid);
    if (me) send_appear(*me, *v);
    if (Session* vs = session_of_gid(g)) send_appear(*vs, *u);
  }
  for (uint32_t g : lost) {
    u->view.erase(g);
    Unit* v = unit_of_gid(g);
    if (me) send(*me, "ZC_NOTIFY_VANISH", {{"gid", wire::I(g)}, {"type", wire::I(0)}});
    if (v) {
      v->view.erase(gid);
      if (Session* vs = session_of_gid(g)) send(*vs, "ZC_NOTIFY_VANISH", {{"gid", wire::I(gid)}, {"type", wire::I(0)}});
    }
  }
}

void Sim::unit_moved(engine::EntityId e, engine::Point from, engine::Point to) {
  auto it = gid_of_ent_.find(ent_key(e));
  if (it == gid_of_ent_.end()) return;
  const uint32_t gid = it->second;
  update_view(gid);
  if (bus_has_hooks("unit:moved")) {
    dispatch("unit:moved", gid);
  }
  (void)from;
  (void)to;
}

void Sim::after_tick_walk_stops() {
  if (walkers_.empty()) return;
  std::vector<uint32_t> stopped;
  for (uint32_t g : walkers_) {
    Unit* u = unit_of_gid(g);
    if (!u || !movement_->walking(u->ent)) stopped.push_back(g);
  }
  for (uint32_t g : stopped) {
    walkers_.erase(std::remove(walkers_.begin(), walkers_.end(), g), walkers_.end());
    Unit* u = unit_of_gid(g);
    if (!u) continue;
    u->walking = false;
    auto pos = position_of(g);
    wire::Bytes f = wire::frame("ZC_STOPMOVE", {{"AID", wire::I(g)}, {"xPos", wire::I(pos->x)}, {"yPos", wire::I(pos->y)}});
    if (Session* me = session_of_gid(g)) send(*me, f);
    std::vector<uint32_t> watchers(u->view.begin(), u->view.end());
    std::sort(watchers.begin(), watchers.end());
    for (uint32_t w : watchers)
      if (Session* vs = session_of_gid(w)) send(*vs, f);
  }
}

// ---- NPC dialogue -------------------------------------------------------------------------------------------------
void Sim::drain_logs_for_npc() {
  for (auto& l : log_capture_)
    if (l.rfind("@run", 0) == 0) npc_log_.push_back(l);
  log_capture_.clear();
}

void Sim::tell_runtime_dialogue_ended(Session& s) {
  if (!s.dlg_sandbox) return;
  auto r = s.dlg_sandbox->run("end", "if SIM_LNPC then SIM_LNPC:endDialogue(" + std::to_string(s.gid) + ") end");
  (void)r;
  s.dlg_sandbox.reset();
}

void Sim::end_dialogue(Session& s, bool notify_client) {
  if (!s.dlg_active) return;
  if (notify_client) send(s, "ZC_CLOSE_DIALOG", {{"npcId", wire::I(s.dlg_npc)}});
  s.dlg_active = false;
  s.dlg_sandbox.reset();
}

void Sim::present_dialogue(Session& s, const script_host::DialogueState& st) {
  drain_logs_for_npc();
  if (st.finished) {
    trace(std::string("dialogue finished s=") + std::to_string(s.idx) + (st.failed ? " FAILED: " + st.error : ""));
    if (st.failed) npc_log_.push_back("@fail\t" + st.error);
    end_dialogue(s, true);
    return;
  }
  s.dlg_token = st.token;
  s.dlg_pending = st.pending;
  const uint32_t npc = s.dlg_npc;
  if (auto* say = std::get_if<script_host::SayRequest>(&st.pending)) {
    send(s, "ZC_SAY_DIALOG", {{"NpcID", wire::I(npc)}, {"message", say->text}});
    send(s, "ZC_WAIT_DIALOG", {{"NpcID", wire::I(npc)}});
  } else if (auto* ch = std::get_if<script_host::ChooseRequest>(&st.pending)) {
    std::string joined;  // the options, each followed by ':' (clif_scriptmenu)
    for (const auto& o : ch->options) joined += o + ":";
    send(s, "ZC_MENU_LIST", {{"npcId", wire::I(npc)}, {"menu", joined}});
  } else if (auto* in = std::get_if<script_host::InputRequest>(&st.pending)) {
    if (!in->prompt.empty()) send(s, "ZC_SAY_DIALOG", {{"NpcID", wire::I(npc)}, {"message", in->prompt}});
    send(s, "ZC_OPEN_EDITDLGSTR", {{"npcId", wire::I(npc)}});
  }  // WaitRequest: the client sees nothing; the timer wakes the dialogue
}

void Sim::on_click_npc(Session& s, const wire::Packet& p) {
  Unit* me = unit_of_gid(s.gid);
  Unit* npc = unit_of_gid(static_cast<uint32_t>(p.num("AID")));  // PACKET_CZ_CONTACTNPC::AID is the npc's id
  if (!me || !npc || npc->kind != 1) return;
  if (!me->view.count(npc->gid)) { trace("npc click outside the view ignored"); return; }
  if (s.dlg_active) { trace("npc click ignored: dialogue already open"); return; }
  if (!has_mod("legacy.npc")) { trace("npc click ignored: legacy.npc is not loaded"); return; }
  try {
    ensure_npc_runtime("legacy.npc");
  } catch (const std::exception& e) {
    npc_log_.push_back(std::string("@fail\t") + e.what());
    return;
  }
  auto& rm = host_->reload_manager();
  const std::string src = "local o = SIM_LNPC:run(" + lq(npc->name) + ", " + std::to_string(s.gid) + ")\nLEGACY.emit_result(o)\n";
  s.dlg_active = true;
  s.dlg_npc = npc->gid;
  s.dlg_mod = "legacy.npc";
  s.dlg_generation = rm.active_generation("legacy.npc");
  s.dlg_sandbox = rm.active_sandbox("legacy.npc");
  script_host::DialogueState st;
  try {
    log_capture_.clear();
    st = dialogues_->start(s.gid, "legacy.npc", src);
  } catch (const std::exception& e) {
    npc_log_.push_back(std::string("@fail\t") + e.what());
    end_dialogue(s, true);
    return;
  }
  trace("dialogue start s=" + std::to_string(s.idx) + " npc=" + npc->name + " gen=" + std::to_string(s.dlg_generation));
  present_dialogue(s, st);
}

void Sim::on_dialogue_reply(Session& s, const wire::Packet& p) {
  if (!s.dlg_active) { trace("dialogue reply without a dialogue ignored: " + p.name); return; }
  // the npc id member differs per packet in the legacy sources: f2 (handler-derived), npc_id, GID
  const uint32_t npc_id = static_cast<uint32_t>(p.name == "CZ_REQ_NEXT_SCRIPT" ? p.num("f2") : p.name == "CZ_CHOOSE_MENU" ? p.num("npc_id") : p.num("GID"));
  if (npc_id != s.dlg_npc) { trace("dialogue reply for another npc ignored"); return; }
  script_host::DialogueResponse resp;
  if (p.name == "CZ_CLOSE_DIALOG" || (p.name == "CZ_CHOOSE_MENU" && p.num("select") == 255)) {
    dialogues_->on_disconnect(s.gid);
    tell_runtime_dialogue_ended(s);
    end_dialogue(s, true);
    return;
  }
  if (p.name == "CZ_CHOOSE_MENU") resp.choice = p.num("select");
  else if (p.name == "CZ_INPUT_EDITDLG") resp.text = std::to_string(p.num("value"));
  else if (p.name == "CZ_INPUT_EDITDLGSTR") resp.text = p.text("value");
  // CZ_REQ_NEXT_SCRIPT: empty response
  try {
    log_capture_.clear();
    script_host::DialogueState st = dialogues_->resume(s.gid, s.dlg_token, resp);
    present_dialogue(s, st);
  } catch (const script_host::DialogueError& e) {
    trace(std::string("dialogue reply rejected: ") + e.what());
    if (!dialogues_->has_dialogue(s.gid)) {  // cancelled underneath (drain timeout, idle timeout)
      tell_runtime_dialogue_ended(s);
      end_dialogue(s, true);
    }
  }
}

void Sim::wake_dialogues() {
  auto wakes = dialogues_->advance_time(static_cast<uint64_t>(clock_.now()));
  for (auto& w : wakes) {
    Session* s = session_of_gid(static_cast<uint32_t>(w.player));
    if (s && s->dlg_active) present_dialogue(*s, w.state);
  }
  for (auto& sp : sessions_) {
    Session& s = *sp;
    if (s.dlg_active && !dialogues_->has_dialogue(s.gid)) {
      tell_runtime_dialogue_ended(s);
      end_dialogue(s, true);
    }
  }
}

Sim::DialogueInfo Sim::dialogue_of(uint32_t player_gid) const {
  DialogueInfo d;
  const Unit* u = unit_of_gid(player_gid);
  if (!u || u->session < 0) return d;
  const Session& s = *sessions_[static_cast<size_t>(u->session)];
  d.active = s.dlg_active;
  d.npc = s.dlg_npc;
  d.generation = s.dlg_generation;
  d.mod = s.dlg_mod;
  return d;
}

// ---- combat ----------------------------------------------------------------------------------------------------------
void Sim::on_act(Session& s, const wire::Packet& p) {
  Unit* me = unit_of_gid(s.gid);
  Unit* t = unit_of_gid(static_cast<uint32_t>(p.num("f2")));  // handler-derived members: target id, action
  const int64_t action = p.num("f6");
  if (!me || !t || (action != 0 && action != 7)) return;
  if (!me->view.count(t->gid)) { trace("attack on a unit outside the view ignored"); return; }
  AttackResult r = basic_attack(me->gid, t->gid);
  if (!r.ok) return;
  wire::Bytes f = wire::frame("ZC_NOTIFY_ACT", {{"srcID", wire::I(me->gid)}, {"targetID", wire::I(t->gid)}, {"serverTick", wire::I(static_cast<int32_t>(server_tick()))},
                                                {"srcSpeed", wire::I(0)}, {"dmgSpeed", wire::I(0)}, {"damage", wire::I(static_cast<int32_t>(r.damage))},
                                                {"isSPDamage", wire::I(0)}, {"div", wire::I(static_cast<uint16_t>(r.div))},
                                                {"type", wire::I(static_cast<uint8_t>(r.type))}, {"damage2", wire::I(static_cast<int32_t>(r.damage2))}});
  send(s, f);
  std::vector<uint32_t> watchers(me->view.begin(), me->view.end());
  std::sort(watchers.begin(), watchers.end());
  for (uint32_t g : watchers)
    if (Session* vs = session_of_gid(g)) send(*vs, f);
}

void Sim::on_skill(Session& s, const wire::Packet& p) {
  // The ruleset packages implement the basic weapon attack only; skill formulas raise NOT_IMPLEMENTED
  // (ruleset-prerenewal scripts/rules/skill.luau). The sim reports that instead of inventing damage.
  trace("skill " + std::to_string(p.num("f4")) + " lv " + std::to_string(p.num("f2")) + " from s=" + std::to_string(s.idx) +
        ": no skill pipeline in the loaded ruleset (basic attack only)");
  unsupported_skill_casts_++;
}

}  // namespace tooling::sim
