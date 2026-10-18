#include "gateway/packet_handlers.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>

namespace gateway {

using namespace wire;
using netproto::FieldValue;

namespace {

std::string format_time(Timestamp t) {
  std::time_t tt = static_cast<std::time_t>(t);
  std::tm tm{};
  gmtime_r(&tt, &tm);
  char buf[24];
  std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tm);
  return buf;
}

int population_code(int users, const UserCountThresholds& th) {  // login_get_usercount
  if (th.disable) return 4;
  if (users <= th.low) return 0;
  if (users <= th.medium) return 1;
  if (users <= th.high) return 2;
  return 3;
}

std::uint32_t ip_le(const std::uint8_t ip[4]) {
  return static_cast<std::uint32_t>(ip[0]) | static_cast<std::uint32_t>(ip[1]) << 8 |
         static_cast<std::uint32_t>(ip[2]) << 16 | static_cast<std::uint32_t>(ip[3]) << 24;
}

}  // namespace

const LanSubnet* lan_subnet_for(std::uint32_t client_ip, const std::vector<LanSubnet>& subnets) {
  for (const LanSubnet& s : subnets) if ((s.net & s.mask) == (client_ip & s.mask)) return &s;
  return nullptr;
}

// ---- base: framing, gating, budget -------------------------------------------------------------------
void ConnectionBase::service_error(HandlerResult& r, Reason why) {
  DecodedPacket p = make("SC_NOTIFY_BAN");
  set_int(p, "result", refuse::ServerClosed);
  send(r, p);
  drop(r, why == Reason::Overloaded ? "overloaded" : "database unavailable");
}

HandlerResult ConnectionBase::feed(ByteView bytes) {
  if (closed_) { HandlerResult r; r.disconnect = true; r.note = "closed"; return r; }
  buf_.insert(buf_.end(), bytes.begin(), bytes.end());
  if (!netproto::buffer_within_limit(buf_.size())) {  // BR-INTER-012
    HandlerResult r;
    drop(r, "receive buffer over limit");
    return r;
  }
  return pump();
}

HandlerResult ConnectionBase::pump() {
  HandlerResult r;
  budget_.begin_cycle();
  while (!closed_ && !buf_.empty()) {
    const netproto::FrameResult fr = codec_.peek(buf_);
    if (fr.status == netproto::FrameStatus::kNeedMore) break;
    if (fr.status == netproto::FrameStatus::kInvalidLength) { drop(r, "invalid declared length"); break; }
    if (fr.status == netproto::FrameStatus::kUnknownOpcode) { drop(r, "unknown opcode"); break; }
    if (!budget_.try_consume()) break;  // the rest waits for the next cycle
    Bytes frame(buf_.begin(), buf_.begin() + fr.length);
    buf_.erase(buf_.begin(), buf_.begin() + fr.length);
    const auto gate = codec_.gate_of(fr.opcode);
    if (!gate || netproto::gate_packet(session_phase(), *gate) != netproto::GateDecision::kAllow) {
      drop(r, "packet not allowed in this phase");
      break;
    }
    try {
      const DecodedPacket p = codec_.decode(frame);
      on_frame(p, fr.opcode, r);
    } catch (const WireError& e) {
      drop(r, std::string("malformed packet: ") + e.what());
    } catch (const StoreUnavailable&) {
      service_error(r, Reason::DatabaseDown);
    } catch (const HashOverloaded&) {
      service_error(r, Reason::Overloaded);
    } catch (const std::exception& e) {
      drop(r, std::string("internal error: ") + e.what());
    }
  }
  return r;
}

// ---- login --------------------------------------------------------------------------------------------
LoginConnection::LoginConnection(const PacketCodec& codec, LoginService& svc, const GatewayConfig& cfg,
                                 std::string peer_ip, ServerListEntry server, std::function<int()> user_count,
                                 UserCountThresholds th, std::vector<LanSubnet> lan, std::uint32_t peer_ipv4)
    : ConnectionBase(codec), svc_(svc), cfg_(cfg), ip_(std::move(peer_ip)), server_(std::move(server)),
      users_(std::move(user_count)), th_(th), lan_(std::move(lan)), ipv4_(peer_ipv4) {}

void LoginConnection::refuse(HandlerResult& r, int code, const std::string& unblock) {
  DecodedPacket p = make("AC_REFUSE_LOGIN");
  set_int(p, "error", code);
  set_str(p, "unblock_time", unblock);
  send(r, p);
}

void LoginConnection::do_login(const LoginRequest& req, HandlerResult& r) {
  const LoginOutcome o = svc_.login(req);
  if (!o.ok) {
    switch (o.reason) {
      case Reason::IpBanned:
        refuse(r, refuse::Rejected);
        drop(r, "ip banned");
        return;
      case Reason::GroupGate: case Reason::WorldDown: case Reason::AlreadyOnline: case Reason::DatabaseDown:
      case Reason::Overloaded: case Reason::RateLimited: case Reason::Internal: {
        DecodedPacket p = make("SC_NOTIFY_BAN");  // logclif_sent_auth_result: 1 closed, 8 still online
        set_int(p, "result", o.refusal_code);
        send(r, p);
        return;
      }
      default:
        refuse(r, o.refusal_code, o.refusal_code == refuse::Banned ? format_time(o.unban_time) : std::string());
        return;
    }
  }
  DecodedPacket p = make("AC_ACCEPT_LOGIN");
  set_int(p, "login_id1", o.ticket->login_id1);
  set_int(p, "AID", o.account_id);
  set_int(p, "login_id2", o.ticket->login_id2);
  set_int(p, "last_ip", 0);
  set_str(p, "last_login", "");
  set_int(p, "sex", wire::sex_to_wire(o.sex));
  set_str(p, "token", o.web_token);
  std::map<std::string, FieldValue> row;
  const LanSubnet* lan = lan_subnet_for(ipv4_, lan_);
  row["ip"] = static_cast<std::int64_t>(ip_le(lan ? lan->ip : server_.ip));
  row["port"] = static_cast<std::int64_t>(server_.port);
  row["name"] = server_.name;
  row["users"] = static_cast<std::int64_t>(population_code(users_ ? users_() : 0, th_));
  row["type"] = static_cast<std::int64_t>(server_.type);
  row["new"] = static_cast<std::int64_t>(server_.is_new);
  p.repeats.push_back(std::move(row));
  send(r, p);
}

void LoginConnection::on_frame(const DecodedPacket& p, std::uint16_t, HandlerResult& r) {
  LoginRequest req;
  req.ip = ip_;
  req.client_hash = client_hash_;
  const std::string& n = p.name;
  if (n == "CA_LOGIN" || n == "CA_LOGIN_PCBANG" || n == "CA_LOGIN_CHANNEL") {
    req.kind = LoginKind::Plaintext;
    req.userid = get_str(p, "username");
    req.secret = get_str(p, "password");
    do_login(req, r);
  } else if (n == "CA_LOGIN2" || n == "CA_LOGIN3" || n == "CA_LOGIN4") {
    if (!cfg_.md5_challenge_enabled) {  // legacy: challenge logins are refused with 3 when MD5 passwords are in use
      refuse(r, refuse::Rejected);
      drop(r, "md5 challenge disabled");
      return;
    }
    req.kind = LoginKind::Md5Challenge;
    req.userid = get_str(p, "username");
    req.secret = hex_of(get_bytes(p, "passwordMD5"));
    req.challenge_salt = md5key_;
    do_login(req, r);
  } else if (n == "CA_SSO_LOGIN_REQ") {
    req.kind = LoginKind::Sso;
    req.userid = get_str(p, "username");
    Bytes tok = get_bytes(p, "token");
    std::string t(tok.begin(), tok.end());
    t = t.substr(0, t.find('\0'));
    req.secret = t.substr(0, 31);  // PASSWD_LENGTH - 1, like safestrncpy in legacy
    do_login(req, r);
  } else if (n == "CA_REQ_HASH") {
    md5key_ = svc_.make_challenge_salt(20);
    DecodedPacket a = make("AC_ACK_HASH");
    set_bytes(a, "salt", Bytes(md5key_.begin(), md5key_.end()));
    send(r, a);
  } else if (n == "CA_EXE_HASHCHECK") {
    client_hash_ = hex_of(get_bytes(p, "hash"));
  } else if (n == "CA_CONNECT_INFO_CHANGED") {
    // keepalive
  } else if (n == "CT_AUTH") {
    DecodedPacket a = make("TC_RESULT");  // legacy answers with a fixed "normal login" result; nothing is granted
    set_int(a, "type", 0);
    set_str(a, "unknown1", "S1000");
    set_str(a, "unknown2", "token");
    send(r, a);
  } else {
    drop(r, "unexpected packet on the login listener");
  }
}

// ---- char select ----------------------------------------------------------------------------------------
CharConnection::CharConnection(const PacketCodec& codec, CharService& svc, const GatewayConfig& cfg, Rng& rng,
                               std::uint64_t conn_id, std::string peer_ip, ServerListEntry world,
                               std::function<std::string(const Character&)> map_name_of)
    : ConnectionBase(codec), rng_(rng), svc_(svc), cfg_(cfg), world_(std::move(world)),
      map_name_of_(std::move(map_name_of)), peer_ip_(std::move(peer_ip)) {
  cs_.conn_id = conn_id;
  cs_.ip = peer_ip_;
}

CharConnection::~CharConnection() {
  if (cs_.phase != rules::SessionPhase::Unauthenticated || cs_.account_id) svc_.on_disconnect(cs_);
}

namespace {
rules::CharPacket category_of(const std::string& n) {
  using rules::CharPacket;
  if (n == "CH_ENTER") return CharPacket::Connect;
  if (n == "PING") return CharPacket::Ping;
  if (n == "CH_SECOND_PASSWD_ACK" || n == "CH_AVAILABLE_SECOND_PASSWD") return CharPacket::PinCheck;
  if (n == "CH_MAKE_SECOND_PASSWD" || n == "CH_EDIT_SECOND_PASSWD") return CharPacket::PinSet;
  if (n == "CH_CHARLIST_REQ") return CharPacket::CharList;
  if (n == "CH_SELECT_CHAR") return CharPacket::SelectChar;
  if (n == "CH_MAKE_CHAR") return CharPacket::MakeChar;
  if (n == "CH_DELETE_CHAR" || n == "CH_DELETE_CHAR3_RESERVED" || n == "CH_DELETE_CHAR3" || n == "CH_DELETE_CHAR3_CANCEL")
    return CharPacket::DeleteChar;
  return CharPacket::Other;
}
}  // namespace

void CharConnection::fill_row(std::map<std::string, FieldValue>& row, const Character& c) {
  auto I = [&](const char* k, std::int64_t v) { row[k] = v; };
  I("GID", c.id);
  I("level", c.base_level);
  row["name"] = c.name;
  I("Str", c.str); I("Agi", c.agi); I("Vit", c.vit); I("Int", c.int_); I("Dex", c.dex); I("Luk", c.luk);
  I("CharNum", c.slot);
  I("hp", c.max_hp); I("maxhp", c.max_hp); I("sp", c.max_sp); I("maxsp", c.max_sp);
  I("bIsChangedCharName", c.rename_counter > 0 ? 0 : 1);
  I("chr_slot_changeCnt", c.slot_moves);
  I("chr_name_changeCnt", c.rename_counter);
  I("sex", wire::sex_to_wire(c.sex));
  row["mapName"] = map_name_of_ ? map_name_of_(c) : std::string();
  I("DelRevDate", c.delete_date ? c.delete_date - svc_.now() : 0);   // remaining seconds, like the legacy field
}

DecodedPacket CharConnection::char_row_packet(const std::string& name, const std::vector<Character>& cs) {
  DecodedPacket p = make(name);
  for (const Character& c : cs) {
    std::map<std::string, FieldValue> row;
    fill_row(row, c);
    p.repeats.push_back(std::move(row));
  }
  return p;
}

void CharConnection::send_pin_wire(HandlerResult& r, int wire_state) {
  cs_.pin_seed = rng_.next_u32() % 0xFFFF;
  DecodedPacket p = make("HC_SECOND_PASSWD_LOGIN");
  set_int(p, "seed", cs_.pin_seed);
  set_int(p, "AID", cs_.account_id);
  set_int(p, "result", wire_state);
  send(r, p);
}

namespace {
// pincode_state values at PACKETVER >= 20180124: PASSED == OK == 0
int pin_wire(PinState s) {
  switch (s) {
    case PinState::Ok: case PinState::Passed: return 0;
    case PinState::Ask: return 1;
    case PinState::Expired: return 3;
    case PinState::New: return 4;
  }
  return 0;
}
constexpr int kPinIllegal = 5, kPinWrong = 8;
}  // namespace

void CharConnection::send_char_list(HandlerResult& r, const EnterOutcome& e) {
  const std::vector<Character> chars = svc_.list(cs_);
  // chclif_mmo_char_send for PACKETVER >= 20130000: 0x082d, 0x006b, 0x09a0, then blocked characters
  DecodedPacket a = make("HC_ACCEPT_ENTER2");
  set_int(a, "normal", cfg_.min_chars);
  set_int(a, "premium", std::max(0, e.slots - cfg_.min_chars));
  set_int(a, "billing", 0);
  set_int(a, "producible", e.slots);
  set_int(a, "total", cfg_.max_chars);
  send(r, a);
  DecodedPacket b = char_row_packet("HC_ACCEPT_ENTER", chars);
  set_int(b, "total", cfg_.max_chars);
  set_int(b, "premium_start", cfg_.min_chars);
  set_int(b, "premium_end", std::max(cfg_.min_chars, e.slots));
  send(r, b);
  DecodedPacket n = make("HC_CHARLIST_NOTIFY");
  set_int(n, "total", std::max(e.slots / 3, 1));
  send(r, n);
  DecodedPacket blk = make("HC_BLOCK_CHARACTER");
  const Timestamp now = svc_.now();
  for (const Character& c : chars) {
    if (c.unban_time > now) {
      std::map<std::string, FieldValue> row;
      row["CID"] = static_cast<std::int64_t>(c.id);
      row["unblock_time"] = format_time(c.unban_time);
      blk.repeats.push_back(std::move(row));
    }
  }
  if (!blk.repeats.empty()) send(r, blk);
}

void CharConnection::on_frame(const DecodedPacket& p, std::uint16_t, HandlerResult& r) {
  const std::string& n = p.name;
  if (cs_.phase != rules::SessionPhase::Unauthenticated || n != "CH_ENTER") {
    // BR-CHAR-022 / S5: until the PIN is verified only connect, ping, PIN and charlist packets pass.
    if (!rules::char_packet_allowed(cs_.phase, category_of(n))) { drop(r, "packet not allowed before PIN / auth"); return; }
  }

  if (n == "CH_ENTER") {
    const AccountId aid = static_cast<AccountId>(get_int(p, "account_id"));
    {  // legacy first echoes the account id as 4 raw bytes
      Bytes ack(4);
      for (int i = 0; i < 4; ++i) ack[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((aid >> (8 * i)) & 0xff);
      r.out.push_back(std::move(ack));
    }
    const std::int64_t sx = get_int(p, "sex");
    if (sx != 0 && sx != 1) { drop(r, "invalid sex"); return; }
    const Sex sex = sx == 1 ? Sex::Male : Sex::Female;
    const EnterOutcome e = svc_.enter(cs_, aid, static_cast<std::uint32_t>(get_int(p, "login_id1")),
                                      static_cast<std::uint32_t>(get_int(p, "login_id2")), sex);
    if (e.ignored) return;
    if (!e.ok) {
      if (e.via_notify_ban) {
        DecodedPacket b = make("SC_NOTIFY_BAN");
        set_int(b, "result", e.refusal_code);
        send(r, b);
      } else {
        DecodedPacket b = make("HC_REFUSE_ENTER");
        set_int(b, "error", e.refusal_code);
        send(r, b);
      }
      drop(r, "enter refused");
      return;
    }
    phase_ = netproto::Phase::PreMapLoad;
    send_char_list(r, e);
    send_pin_wire(r, pin_wire(e.pin));
    return;
  }

  if (n == "PING") {
    if (get_int(p, "AID") != cs_.account_id) drop(r, "ping with foreign account id");
    return;
  }
  if (n == "CH_CHARLIST_REQ") {
    send(r, char_row_packet("HC_ACK_CHARINFO_PER_PAGE", svc_.list(cs_)));
    return;
  }
  if (n == "CH_AVAILABLE_SECOND_PASSWD") {
    if (get_int(p, "AID") != cs_.account_id || !cfg_.pin_enabled) { drop(r, "pin window request"); return; }
    send_pin_wire(r, pin_wire(svc_.pin_window_state(cs_)));
    return;
  }
  if (n == "CH_SECOND_PASSWD_ACK" || n == "CH_MAKE_SECOND_PASSWD" || n == "CH_EDIT_SECOND_PASSWD") {
    if (get_int(p, "AID") != cs_.account_id || !cfg_.pin_enabled) { drop(r, "pin packet"); return; }
    auto digits = [&](const char* f) {
      const Bytes b = get_bytes(p, f);
      return pin_untransport(cs_.pin_seed, std::string(b.begin(), b.end()));
    };
    if (n == "CH_SECOND_PASSWD_ACK") {
      const std::string pin = digits("pin");
      if (pin.empty()) { drop(r, "malformed pin"); return; }
      const PinOutcome o = svc_.pin_check(cs_, pin);
      if (o.ok) send_pin_wire(r, 0);
      else {
        send_pin_wire(r, kPinWrong);
        if (o.account_offlined) drop(r, "pin lockout");
      }
    } else if (n == "CH_MAKE_SECOND_PASSWD") {
      const std::string pin = digits("pin");
      if (pin.empty()) { drop(r, "malformed pin"); return; }
      if (!svc_.pin_is_legal(pin)) { send_pin_wire(r, kPinIllegal); return; }
      // An existing PIN cannot be replaced through this packet (legacy allowed it without the old PIN).
      if (svc_.pin_set(cs_, "", pin)) send_pin_wire(r, 0);
      else send_pin_wire(r, kPinWrong);
    } else {
      const std::string oldp = digits("old_pin"), newp = digits("new_pin");
      if (oldp.empty() || newp.empty()) { drop(r, "malformed pin"); return; }
      if (!svc_.pin_is_legal(newp)) { send_pin_wire(r, kPinIllegal); return; }
      if (svc_.pin_set(cs_, oldp, newp)) send_pin_wire(r, 0);
      else {
        send_pin_wire(r, kPinWrong);
        if (cs_.phase == rules::SessionPhase::Unauthenticated) drop(r, "pin lockout");
      }
    }
    return;
  }
  if (n == "CH_SELECT_CHAR") {
    const SelectOutcome o = svc_.select(cs_, static_cast<int>(get_int(p, "slot")));
    if (!o.ok) {
      if (o.code == 1) { DecodedPacket b = make("SC_NOTIFY_BAN"); set_int(b, "result", 1); send(r, b); }
      else { DecodedPacket b = make("HC_REFUSE_ENTER"); set_int(b, "error", 0); send(r, b); }
      return;
    }
    DecodedPacket z = make("HC_NOTIFY_ZONESVR");
    set_int(z, "CID", o.ch.id);
    set_str(z, "mapname", map_name_of_ ? map_name_of_(o.ch) : std::string());
    set_int(z, "ip", ip_le(world_.ip));
    set_int(z, "port", world_.port);
    send(r, z);
    return;
  }
  if (n == "CH_MAKE_CHAR") {
    MakeCharRequest m;
    m.name = get_str(p, "name");
    m.slot = static_cast<int>(get_int(p, "slot"));
    m.new_client = true;
    const std::int64_t sx = get_int(p, "sex");
    m.sex = sx == 1 ? Sex::Male : sx == 0 ? Sex::Female : Sex::Server;
    m.start_job_flag = svc_.job_flag_for_client_job(static_cast<std::uint32_t>(get_int(p, "job")));
    m.client_choices["hair_color"] = get_int(p, "hair_color");
    m.client_choices["hair_style"] = get_int(p, "hair_style");
    m.client_choices["job"] = get_int(p, "job");
    const MakeCharOutcome o = svc_.make(cs_, m);
    if (!o.ok) {
      DecodedPacket b = make("HC_REFUSE_MAKECHAR");
      set_int(b, "error", rules::create_error_wire_code(o.error));
      send(r, b);
      return;
    }
    DecodedPacket b = make("HC_ACCEPT_MAKECHAR");
    std::map<std::string, FieldValue> row;
    fill_row(row, o.ch);
    b.fields = row;
    send(r, b);
    return;
  }
  if (n == "CH_DELETE_CHAR") {
    const DeleteOutcome o = svc_.confirm_delete(cs_, static_cast<CharId>(get_int(p, "CID")), get_str(p, "key"));
    if (o.ok) { send(r, make("HC_ACCEPT_DELETECHAR")); return; }
    int err = 0;  // chclif_parse_delchar mapping
    switch (o.result) {
      case DeleteResult::NotFound: err = 1; break;
      case DeleteResult::Guild: case DeleteResult::Party: err = 2; break;
      default: err = 0; break;
    }
    DecodedPacket b = make("HC_REFUSE_DELETECHAR");
    set_int(b, "error", err);
    send(r, b);
    return;
  }
  if (n == "CH_DELETE_CHAR3_RESERVED") {
    const CharId cid = static_cast<CharId>(get_int(p, "CID"));
    const DeleteOutcome o = svc_.request_delete(cs_, cid);
    DecodedPacket b = make("HC_DELETE_CHAR3_RESERVED");
    set_int(b, "CID", cid);
    set_int(b, "result", o.code);
    set_int(b, "date", o.ok ? o.remaining : 0);
    send(r, b);
    return;
  }
  if (n == "CH_DELETE_CHAR3") {
    const CharId cid = static_cast<CharId>(get_int(p, "CID"));
    const Bytes bd = get_bytes(p, "birthdate");
    const DeleteOutcome o = svc_.confirm_delete(cs_, cid, std::string(bd.begin(), bd.end()), 2);
    int code = 3;
    switch (o.result) {
      case DeleteResult::Ok: code = 1; break;
      case DeleteResult::Party: case DeleteResult::Guild: case DeleteResult::BaseLevel: code = 2; break;
      case DeleteResult::Time: code = 4; break;
      case DeleteResult::BadCode: code = 5; break;
      default: code = 3; break;
    }
    if (o.ok) {
      EnterOutcome e;
      e.slots = 0;
      e.slots = cfg_.max_chars;
      send_char_list(r, e);
    }
    DecodedPacket b = make("HC_DELETE_CHAR3");
    set_int(b, "CID", cid);
    set_int(b, "result", code);
    send(r, b);
    return;
  }
  if (n == "CH_DELETE_CHAR3_CANCEL") {
    const CharId cid = static_cast<CharId>(get_int(p, "CID"));
    const DeleteOutcome o = svc_.cancel_delete(cs_, cid);
    DecodedPacket b = make("HC_DELETE_CHAR3_CANCEL");
    set_int(b, "CID", cid);
    set_int(b, "result", o.ok ? 1 : 2);
    send(r, b);
    return;
  }
  if (n == "CH_REQ_IS_VALID_CHARNAME") {
    if (get_int(p, "AID") != cs_.account_id) { drop(r, "foreign account id"); return; }
    DecodedPacket b = make("HC_ACK_IS_VALID_CHARNAME");
    set_int(b, "result", svc_.name_acceptable(cs_, static_cast<CharId>(get_int(p, "CID")), get_str(p, "new_name")) ? 1 : 0);
    send(r, b);
    return;
  }
  if (n == "CH_REQ_CHANGE_CHARNAME") {
    const int code = svc_.rename_code(cs_, static_cast<CharId>(get_int(p, "CID")), get_str(p, "new_name"));
    DecodedPacket b = make("HC_ACK_CHANGE_CHARNAME");
    set_int(b, "result", code);
    send(r, b);
    if (code == 0) { EnterOutcome e; e.slots = cfg_.max_chars; send_char_list(r, e); }
    return;
  }
  if (n == "CH_REQ_CHANGE_CHARACTER_SLOT") {
    int moves = 0;
    const int from = static_cast<int>(get_int(p, "slot_before"));
    const int reason = svc_.move_slot_by_slot(cs_, from, static_cast<int>(get_int(p, "slot_after")), &moves);
    DecodedPacket b = make("HC_ACK_CHANGE_CHARACTER_SLOT");
    set_int(b, "reason", reason);
    set_int(b, "moves", moves);
    send(r, b);
    if (reason == 0) { EnterOutcome e; e.slots = cfg_.max_chars; send_char_list(r, e); }
    return;
  }
  drop(r, "unhandled char packet");
}

// ---- world connect --------------------------------------------------------------------------------------
void WorldConnection::on_frame(const DecodedPacket& p, std::uint16_t, HandlerResult& r) {
  if (p.name != "CZ_ENTER") { if (!authed_) drop(r, "unexpected packet before world connect"); return; }
  netproto::ConnectPacket cp;
  cp.account_id = get_int(p, "account_id");
  cp.char_id = get_int(p, "char_id");
  cp.login_id1 = static_cast<std::uint32_t>(get_int(p, "auth_code"));
  cp.client_tick = static_cast<std::uint32_t>(get_int(p, "client_tick"));
  cp.sex = static_cast<std::uint8_t>(get_int(p, "sex"));
  const auto* res = codec_.resolved("CZ_ENTER");
  const std::size_t expected = res->version->length.fixed_size;
  const netproto::ConnectError err = netproto::validate_connect(expected, expected, cp, authed_);  // BR-MAP-060
  if (err == netproto::ConnectError::kAlreadyLoggedIn) return;  // ignored, like legacy
  if (err != netproto::ConnectError::kNone) {
    DecodedPacket b = make("ZC_REFUSE_ENTER");
    set_int(b, "error", netproto::refusal_code(err));
    send(r, b);
    drop(r, "invalid connect packet");
    return;
  }
  const Sex sex = cp.sex == 1 ? Sex::Male : Sex::Female;
  if (!svc_.authenticate_from_world(static_cast<AccountId>(cp.account_id), static_cast<CharId>(cp.char_id),
                                    cp.login_id1, &sex)) {
    DecodedPacket b = make("SC_NOTIFY_BAN");  // clif_authfail_fd(fd, 0)
    set_int(b, "result", 0);
    send(r, b);
    drop(r, "world auth refused");
    return;
  }
  authed_ = true;
  phase_ = netproto::Phase::PreMapLoad;
  r.world_entered = true;
  r.account_id = static_cast<AccountId>(cp.account_id);
  r.char_id = static_cast<CharId>(cp.char_id);
}

}  // namespace gateway
