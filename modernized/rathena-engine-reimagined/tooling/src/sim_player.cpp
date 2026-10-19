#include "tooling/sim_player.hpp"

#include <algorithm>
#include <iterator>

namespace tooling::sim {

SimPlayer::SimPlayer(Sim& sim, int session, std::string name)
    : sim_(sim), session_(session), name_(std::move(name)), client_(sim.codec_ptr(), std::nullopt) {}

uint32_t SimPlayer::aid() const { return sim_.session(session_).aid; }
uint32_t SimPlayer::gid() const { return sim_.session(session_).gid; }
bool SimPlayer::connected() const {
  const auto& s = sim_.session(session_);
  return s.online && s.phase == netproto::Phase::InWorld;
}
bool SimPlayer::disconnected() const { return !sim_.session(session_).disconnect_reason.empty() && !sim_.session(session_).online; }
std::string SimPlayer::disconnect_reason() const { return sim_.session(session_).disconnect_reason; }

void SimPlayer::send_frame(const wire::Bytes& f) { sim_.client_send(session_, f); }
void SimPlayer::send_raw(const wire::Bytes& frame) { send_frame(frame); }
void SimPlayer::send_packet(const wire::DecodedPacket& p) {
  // A refused or banned client is closed (SimClient::send throws); the server dropped the session anyway, so a
  // late packet is still encoded and handed to the dead session, which ignores it (as the sim always did).
  send_frame(client_.closed() ? sim_.codecs().encode(p) : client_.send(p));
}
void SimPlayer::send_packet(const std::string& name, wire::Fields fields) {
  wire::DecodedPacket p;
  p.name = name;
  for (const auto& [k, v] : fields) p.fields[k] = v;
  send_packet(p);
}

// ---- connect --------------------------------------------------------------------------------------------
void SimPlayer::begin_connect() {
  const uint32_t login_id1 = sim_.rng().derive("login:" + name_).next();
  send_packet("CZ_ENTER", {{"account_id", wire::I(aid())}, {"char_id", wire::I(aid())}, {"login_id1", wire::I(login_id1)},
                           {"client_tick", wire::I(sim_.server_tick())}, {"sex", wire::I(1)}});
}

void SimPlayer::finish_connect() {
  auto acc = wait_for("accept", 2000);
  if (!acc) {
    if (auto ref = wait_for("refuse", 0)) throw SimError(name_ + ": connect refused (code " + std::to_string(ref->num("errorCode")) + ")");
    throw SimError(name_ + ": no ZC_ACCEPT_ENTER within 2000 ms; inbox: " + describe_inbox());
  }
  accepted_ = true;
  known_pos_ = engine::Point{acc->pos3("posDir").x, acc->pos3("posDir").y};
  send_packet("CZ_NOTIFY_ACTORINIT", {});
  if (!sim_.run_until([&] { return connected(); }, 2000)) throw SimError(name_ + ": the world did not open after ZC_ACCEPT_ENTER");
}

void SimPlayer::connect() {
  begin_connect();
  finish_connect();
}

void SimPlayer::connect_all(Sim& sim, const std::vector<SimPlayer*>& players) {
  for (auto* p : players) p->begin_connect();
  // one batch: everybody is accepted within the same ticks
  if (!sim.run_until(
          [&] {
            for (auto* p : players) {
              p->pump();
              if (!p->history().empty() || p->disconnected()) continue;
              return false;
            }
            return true;
          },
          5000))
    throw SimError("some players received no answer to CZ_ENTER within 5000 ms");
  for (auto* p : players) {
    auto acc = p->wait_for("accept", 0);
    if (!acc) throw SimError(p->name() + ": connect refused or unanswered; inbox: " + p->describe_inbox());
    p->accepted_ = true;
    p->known_pos_ = engine::Point{acc->pos3("posDir").x, acc->pos3("posDir").y};
    p->send_packet("CZ_NOTIFY_ACTORINIT", {});
  }
  if (!sim.run_until(
          [&] {
            for (auto* p : players)
              if (!p->connected()) return false;
            return true;
          },
          5000))
    throw SimError("the world did not open for every player within 5000 ms");
}

// ---- actions ---------------------------------------------------------------------------------------------
void SimPlayer::walk(int x, int y) {
  send_packet("CZ_REQUEST_MOVE", {{"pos", netproto::Pos3{static_cast<uint16_t>(x), static_cast<uint16_t>(y), 0}}});
}
void SimPlayer::say(const std::string& text) { send_packet("CZ_REQUEST_CHAT", {{"tail", wire::chat_tail(name_ + " : " + text)}}); }
void SimPlayer::click_npc(const std::string& npc_name) { click_npc_gid(sim_.npc_gid(npc_name)); }
void SimPlayer::click_npc_gid(uint32_t npc_gid) { send_packet("CZ_CONTACTNPC", {{"AID", wire::I(npc_gid)}, {"type", wire::I(1)}}); }
void SimPlayer::answer(int choice) {
  send_packet("CZ_CHOOSE_MENU", {{"npc_id", wire::I(sim_.session(session_).dlg_npc)}, {"select", wire::I(static_cast<uint8_t>(choice))}});
}
void SimPlayer::next() { send_packet("CZ_REQ_NEXT_SCRIPT", {{"f2", wire::I(sim_.session(session_).dlg_npc)}}); }
void SimPlayer::input(const std::string& text) {
  send_packet("CZ_INPUT_EDITDLGSTR", {{"GID", wire::I(sim_.session(session_).dlg_npc)}, {"value", text}});
}
void SimPlayer::input_number(int32_t v) { send_packet("CZ_INPUT_EDITDLG", {{"GID", wire::I(sim_.session(session_).dlg_npc)}, {"value", wire::I(v)}}); }
void SimPlayer::close_dialog() { send_packet("CZ_CLOSE_DIALOG", {{"GID", wire::I(sim_.session(session_).dlg_npc)}}); }
void SimPlayer::ping() { send_packet("CZ_REQUEST_TIME", {{"client_tick", wire::I(sim_.server_tick())}}); }
void SimPlayer::attack(uint32_t target_gid) { send_packet("CZ_REQUEST_ACT2", {{"f2", wire::I(target_gid)}, {"f6", wire::I(0)}}); }
void SimPlayer::cast(const std::string& skill_id, uint32_t target_gid, uint16_t level) {
  send_packet("CZ_USE_SKILL2", {{"f2", wire::I(level)}, {"f4", wire::I(sim_.skill_client_id(skill_id))}, {"f6", wire::I(target_gid)}});
}
void SimPlayer::disconnect() { sim_.client_close(session_); }

// ---- receiving ---------------------------------------------------------------------------------------------
void SimPlayer::pump() {
  wire::Bytes in = sim_.client_take(session_);
  if (in.empty()) return;
  rx_.insert(rx_.end(), in.begin(), in.end());
  auto sp = wire::split_frames(rx_, sim_.s2c_table());
  rx_.erase(rx_.begin(), rx_.begin() + static_cast<std::ptrdiff_t>(sp.consumed));
  std::string error = sp.error;
  for (auto& f : sp.frames) {
    // one complete frame at a time, so the event keeps its bytes; the SimClient decodes it and tracks the phase
    auto got = client_.receive(f);
    if (got.empty()) {
      if (error.empty()) error = client_.closed() ? client_.close_reason() : "undecodable server frame";
      break;
    }
    Event e = wire::make_packet(std::move(got.front()), f, static_cast<uint16_t>(f[0] | (f[1] << 8)));
    e.at_ms = sim_.now_ms();
    feed_state(e);
    history_.push_back(e);
    inbox_.push_back(std::move(e));
    if (history_.size() > history_cap_) history_.pop_front();
    if (inbox_.size() > history_cap_) inbox_.pop_front();
  }
  if (!error.empty()) {
    Event e;
    e.name = "protocol_error";
    e.fields["error"] = error;
    e.at_ms = sim_.now_ms();
    inbox_.push_back(e);
    history_.push_back(e);
  }
}

void SimPlayer::feed_state(const Event& e) {
  if (e.name == "ZC_ACCEPT_ENTER") {
    known_pos_ = engine::Point{e.pos3("posDir").x, e.pos3("posDir").y};
  } else if (e.name == "LEGACY_IDLE_UNIT" || e.name == "LEGACY_UNIT_WALKING") {
    seen_.insert(static_cast<uint32_t>(e.num("GID")));
  } else if (e.name == "ZC_NOTIFY_VANISH") {
    seen_.erase(static_cast<uint32_t>(e.num("gid")));
  } else if (e.name == "ZC_STOPMOVE" && static_cast<uint32_t>(e.num("AID")) == gid()) {
    known_pos_ = engine::Point{static_cast<int32_t>(e.num("xPos")), static_cast<int32_t>(e.num("yPos"))};
  }
}

std::string SimPlayer::alias(const std::string& n) {
  static const std::map<std::string, std::string> a = {
      {"accept", "ZC_ACCEPT_ENTER"},        {"refuse", "ZC_REFUSE_ENTER"},       {"time", "ZC_NOTIFY_TIME"},
      {"move", "ZC_NOTIFY_PLAYERMOVE"},     {"stop", "ZC_STOPMOVE"},             {"vanish", "ZC_NOTIFY_VANISH"},
      {"standing", "LEGACY_IDLE_UNIT"},     {"moving", "LEGACY_UNIT_WALKING"},   {"chat", "ZC_NOTIFY_CHAT"},
      {"chat_self", "OPAQUE_008E"},         {"say", "ZC_SAY_DIALOG"},            {"next", "ZC_WAIT_DIALOG"},
      {"close", "ZC_CLOSE_DIALOG"},         {"menu", "ZC_MENU_LIST"},            {"input", "ZC_OPEN_EDITDLGSTR"},
      {"damage", "ZC_NOTIFY_ACT"}};
  auto it = a.find(n);
  return it == a.end() ? n : it->second;
}

bool SimPlayer::matches(const Event& e, const std::string& want) const {
  if (want == "appear") return e.name == "LEGACY_IDLE_UNIT" || e.name == "LEGACY_UNIT_WALKING";
  return e.name == want || e.name == alias(want);
}

std::vector<SimPlayer::Event> SimPlayer::drain_received() {
  std::vector<Event> out(std::make_move_iterator(inbox_.begin()), std::make_move_iterator(inbox_.end()));
  inbox_.clear();
  return out;
}

std::optional<SimPlayer::Event> SimPlayer::wait_for(const std::string& name, int64_t timeout_ms) {
  auto take = [&]() -> std::optional<Event> {
    for (auto it = inbox_.begin(); it != inbox_.end(); ++it) {
      if (matches(*it, name)) {
        Event e = std::move(*it);
        inbox_.erase(it);
        return e;
      }
    }
    return std::nullopt;
  };
  pump();
  if (auto e = take()) return e;
  int64_t spent = 0;
  while (spent < timeout_ms) {
    const int64_t dt = std::min<int64_t>(sim_.config().tick_ms, timeout_ms - spent);
    sim_.run_for(dt);
    spent += dt;
    if (auto e = take()) return e;
  }
  return std::nullopt;
}

SimPlayer::Event SimPlayer::expect(const std::string& name, int64_t timeout_ms) {
  if (auto e = wait_for(name, timeout_ms)) return *e;
  throw SimError(name_ + ": expected event '" + name + "' within " + std::to_string(timeout_ms) + " ms of virtual time; inbox: " + describe_inbox());
}

bool SimPlayer::expect_none(const std::string& name, int64_t within_ms) {
  auto e = wait_for(name, within_ms);
  return !e.has_value();
}

std::optional<SimPlayer::Event> SimPlayer::last(const std::string& name) const {
  for (auto it = history_.rbegin(); it != history_.rend(); ++it)
    if (matches(*it, name)) return *it;
  return std::nullopt;
}

size_t SimPlayer::count(const std::string& name) const {
  size_t n = 0;
  for (auto& e : history_)
    if (matches(e, name)) ++n;
  return n;
}

std::string SimPlayer::describe_inbox() const {
  if (inbox_.empty()) return "(empty)";
  std::string s;
  size_t n = 0;
  for (auto& e : inbox_) {
    if (n++) s += ", ";
    s += e.name;
    if (n >= 12) { s += ", ..."; break; }
  }
  return s;
}

std::optional<engine::Point> SimPlayer::position() const { return sim_.position_of(gid()); }

}  // namespace tooling::sim
