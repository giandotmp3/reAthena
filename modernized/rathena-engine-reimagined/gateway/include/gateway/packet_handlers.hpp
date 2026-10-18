#pragma once
// Per-connection packet handlers for the three gateway listeners (login, char-select, world connect).
// Each connection object owns its receive buffer and speaks the wire.hpp codec: bytes in, reply
// frames out, plus a disconnect decision. They contain no policy; every decision is a service call.
//
// Framing, unknown opcodes, declared-length validation and the receive-buffer bound come from
// netproto (peek_frame / FrameLimits); phase gating from netproto::gate_packet; the PIN gate from
// rules::char_packet_allowed; at most `frames_per_cycle` frames are processed per pump (netproto
// CycleBudget): the rest stay buffered for the next cycle, delayed and not dropped.
#include <functional>
#include <string>
#include <vector>

#include "gateway/services.hpp"
#include "gateway/wire.hpp"

namespace gateway {

struct HandlerResult {
  std::vector<wire::Bytes> out;   // frames (or the one raw 4-byte account id ack) to send, in order
  bool disconnect = false;
  std::string note;               // why, for logs and tests
  // World connect only:
  bool world_entered = false;
  AccountId account_id = 0;
  CharId char_id = 0;
};

struct ServerListEntry {
  std::string name = "rathena";
  std::uint8_t ip[4] = {127, 0, 0, 1};   // same host and port for login, char and world: one process
  std::uint16_t port = 6900;
  std::uint16_t type = 0;
  std::uint16_t is_new = 0;
};

struct LanSubnet { std::uint32_t net = 0, mask = 0; std::uint8_t ip[4] = {0, 0, 0, 0}; };
/// BR-LOGIN-020: the address a client inside `subnets` is told to use, else nullptr (WAN).
const LanSubnet* lan_subnet_for(std::uint32_t client_ip, const std::vector<LanSubnet>& subnets);

struct UserCountThresholds { int low = 200, medium = 500, high = 1000; bool disable = false; };

class ConnectionBase {
 public:
  explicit ConnectionBase(const wire::PacketCodec& codec, std::uint32_t frames_per_cycle = 3)
      : codec_(codec), budget_(frames_per_cycle) {}
  virtual ~ConnectionBase() = default;
  /// Append received bytes and process up to one cycle's worth of frames.
  HandlerResult feed(wire::ByteView bytes);
  /// Next cycle: process frames still buffered.
  HandlerResult pump();
  bool has_pending() const { return !buf_.empty(); }
  bool closed() const { return closed_; }
 protected:
  virtual void on_frame(const netproto::DecodedPacket& p, std::uint16_t opcode, HandlerResult& r) = 0;
  virtual netproto::Phase session_phase() const = 0;
  void send(HandlerResult& r, const netproto::DecodedPacket& p) { r.out.push_back(codec_.encode(p)); }
  void drop(HandlerResult& r, const std::string& why) { r.disconnect = true; r.note = why; closed_ = true; }
  /// A service threw StoreUnavailable (database down / writes refused) or HashOverloaded: answer
  /// "server closed" (SC_NOTIFY_BAN 1) and disconnect. Never lets an exception reach the IO thread.
  virtual void service_error(HandlerResult& r, Reason why);
  const wire::PacketCodec& codec_;
 private:
  wire::Bytes buf_;
  netproto::CycleBudget budget_;
  bool closed_ = false;
};

// ---- login listener ------------------------------------------------------------------------------
class LoginConnection : public ConnectionBase {
 public:
  LoginConnection(const wire::PacketCodec& codec, LoginService& svc, const GatewayConfig& cfg, std::string peer_ip,
                  ServerListEntry server, std::function<int()> user_count, UserCountThresholds th = {},
                  std::vector<LanSubnet> lan = {}, std::uint32_t peer_ipv4 = 0);
 protected:
  void on_frame(const netproto::DecodedPacket& p, std::uint16_t opcode, HandlerResult& r) override;
  netproto::Phase session_phase() const override { return netproto::Phase::PreSession; }
 private:
  void do_login(const LoginRequest& req, HandlerResult& r);
  void refuse(HandlerResult& r, int code, const std::string& unblock = {});
  LoginService& svc_;
  const GatewayConfig& cfg_;
  std::string ip_;
  ServerListEntry server_;
  std::function<int()> users_;
  UserCountThresholds th_;
  std::vector<LanSubnet> lan_;
  std::uint32_t ipv4_;
  std::string md5key_;
  std::string client_hash_;
};

// ---- char-select listener ----------------------------------------------------------------------------
class CharConnection : public ConnectionBase {
 public:
  CharConnection(const wire::PacketCodec& codec, CharService& svc, const GatewayConfig& cfg, Rng& rng,
                 std::uint64_t conn_id, std::string peer_ip, ServerListEntry world,
                 std::function<std::string(const Character&)> map_name_of = {});
  ~CharConnection() override;
  const CharSession& session() const { return cs_; }
  /// Bytes the world would send after the handoff (HC_NOTIFY_ZONESVR) are produced by select().
 protected:
  void on_frame(const netproto::DecodedPacket& p, std::uint16_t opcode, HandlerResult& r) override;
  netproto::Phase session_phase() const override { return phase_; }
 private:
  void send_char_list(HandlerResult& r, const EnterOutcome& e);
  void send_pin_wire(HandlerResult& r, int wire_state);
  netproto::DecodedPacket char_row_packet(const std::string& name, const std::vector<Character>& cs);
  void fill_row(std::map<std::string, netproto::FieldValue>& row, const Character& c);
  Rng& rng_;
  CharService& svc_;
  const GatewayConfig& cfg_;
  CharSession cs_;
  ServerListEntry world_;
  netproto::Phase phase_ = netproto::Phase::PreSession;
  std::function<std::string(const Character&)> map_name_of_;
  std::string peer_ip_;
};

// ---- world connect (CZ_ENTER 0x0436) -------------------------------------------------------------------
class WorldConnection : public ConnectionBase {
 public:
  WorldConnection(const wire::PacketCodec& codec, CharService& svc) : ConnectionBase(codec), svc_(svc) {}
 protected:
  void on_frame(const netproto::DecodedPacket& p, std::uint16_t opcode, HandlerResult& r) override;
  netproto::Phase session_phase() const override { return phase_; }
 private:
  CharService& svc_;
  netproto::Phase phase_ = netproto::Phase::PreSession;
  bool authed_ = false;
};

}  // namespace gateway
