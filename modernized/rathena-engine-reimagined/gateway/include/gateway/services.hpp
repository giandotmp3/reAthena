#pragma once
// Gateway services: login pipeline (CAP-01..04), account administration (CAP-02), character
// select / lifecycle / PIN plumbing (CAP-05, 06). Mechanism only: every number that is policy comes
// from GatewayConfig or PolicyHooks.
//
// Concurrency: when GatewayDeps::strand is set every public method takes that gateway-private mutex.
// LoginService releases it while a password is being hashed, so hashing on the bounded pool never
// serialises the other logins. No service ever touches simulation-thread state.
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "gateway/clock.hpp"
#include "gateway/config.hpp"
#include "gateway/inproc.hpp"
#include "gateway/policy.hpp"
#include "gateway/rules.hpp"
#include "gateway/seams.hpp"

namespace gateway {

struct GatewayDeps {
  const GatewayConfig& cfg;
  Clock& clock;
  Rng& rng;
  AccountStore& accounts;
  CharacterStore& chars;
  SessionDirectory& directory;
  WorldBus& bus;
  TicketStore& tickets;
  PasswordHasher& hasher;
  InProcIpBanList& ip_bans;
  InProcFailureCounter& failures;
  const PolicyHooks& policy;
  // Optional collaborators (nullptr = feature off).
  DnsblResolver* dnsbl = nullptr;
  AuditSink* audit = nullptr;
  AccountRegistryPurger* registry = nullptr;
  std::mutex* strand = nullptr;
};

// ---- Login ----------------------------------------------------------------------------------
class LoginService {
 public:
  explicit LoginService(GatewayDeps d);
  // BR-LOGIN-001..012, 013..019, 022, 023, 034, 039; lazy rehash. Never throws: store outages,
  // hash-queue overload and every internal error become a refusal with a Reason.
  LoginOutcome login(const LoginRequest& req);
  // Called on logout/disconnect: clears the online entry, schedules the web-token disable (BR-LOGIN-034).
  void on_logout(AccountId aid);
  // BR-LOGIN-018/033/024: forced duplicate clear, purge of lost entries, expired IP bans, idle counters.
  void sweep_duplicate_clears();
  // BR-LOGIN-034: web tokens are wiped at startup.
  void startup();
  // BR-LOGIN-005: bytes 1..255, never NUL.
  std::string make_challenge_salt(std::size_t len = 16);
  std::size_t pending_timers() const;
 private:
  struct Ctx;
  LoginOutcome run(std::unique_lock<std::mutex>& lk, const LoginRequest& req);
  LoginOutcome refuse(const LoginRequest& req, int code, Reason why, Timestamp unban = 0);
  void audit_result(const LoginRequest& req, const LoginOutcome& o);
  bool check_password(std::unique_lock<std::mutex>& lk, Ctx& c, const LoginRequest& req);
  std::optional<LoginOutcome> try_register(std::unique_lock<std::mutex>& lk, const LoginRequest& req, std::string& userid);
  std::string new_web_token(AccountId aid);
  void arm_login_timeout(AccountId aid, std::uint32_t gen);
  void arm_forced_clear(AccountId aid);
  GatewayDeps d_;
  std::set<AccountId> clear_pending_;
  // registration flood window (BR-LOGIN-007)
  Timestamp reg_window_end_ = 0;
  int reg_count_ = 0;
  std::map<AccountId, int> logout_gen_;
};

// ---- Account administration (CAP-02) -------------------------------------------------------
class AccountAdmin {
 public:
  explicit AccountAdmin(GatewayDeps d) : d_(d) {}
  bool ban(AccountId aid, Timestamp delta);                 // BR-LOGIN-026 (broadcasts)
  void unban(AccountId aid);                                // BR-LOGIN-027
  void set_state(AccountId aid, int state);                 // BR-LOGIN-028
  bool toggle_sex(AccountId aid);                           // BR-LOGIN-029
  bool change_email(AccountId aid, const std::string& current, const std::string& next);  // 030
  int char_slots(AccountId aid) const;                      // BR-LOGIN-031
  void apply_vip(AccountId aid, bool active);               // BR-LOGIN-032
  bool delete_account(AccountId aid);                       // BR-LOGIN-037 (atomic)
  // BR-CHAR-055: authority is re-checked here, not trusted from the caller. A requester may change a
  // target only if the requester's group is >= min_gm_group and not lower than the target's group.
  bool gm_set_state(AccountId requester, AccountId target, int state);
  // BR-LOGIN-035
  void record_pin_update(AccountId aid, const std::string& pin_hash, Timestamp when);
  static bool email_is_valid(const std::string& e);
  bool gm_ban(AccountId requester, AccountId target, Timestamp delta);
  bool gm_unban(AccountId requester, AccountId target);
  bool gm_toggle_sex(AccountId requester, AccountId target);
  bool gm_apply_vip(AccountId requester, AccountId target, bool active);
 private:
  bool gm_authorized(AccountId requester, AccountId target) const;
  GatewayDeps d_;
};

// ---- Character plumbing (CAP-05, 06) ---------------------------------------------------------
enum class PinState { Ok = 0, New = 1, Passed = 2, Ask = 3, Expired = 4 };

struct CharSession {
  AccountId account_id = 0;
  std::uint32_t login_id1 = 0, login_id2 = 0;
  Sex sex = Sex::Male;
  std::string ip;
  rules::SessionPhase phase = rules::SessionPhase::Unauthenticated;
  bool pin_verified = false;
  std::uint64_t conn_id = 0;     // 0 = unknown (skips the "second connection" check)
  std::uint32_t pin_seed = 0;
  int pin_tries = 0;             // wrong PINs in this session (the account-level counter survives reconnects)
  bool returning_from_world = false;
};

struct EnterOutcome {
  bool ok = false;
  int refusal_code = 0;
  bool via_notify_ban = false;  // true: answer with SC_NOTIFY_BAN(refusal_code); false: HC_REFUSE_ENTER(refusal_code)
  bool ignored = false;         // already-authenticated session sent the enter packet again: discard silently
  PinState pin = PinState::Ok;
  int slots = 0;
};

struct MakeCharRequest {
  std::string name;
  int slot = 0;
  rules::StartStats stats{1, 1, 1, 1, 1, 1};
  bool new_client = true;       // PACKETVER >= 20151001
  Sex sex = Sex::Male;          // client-chosen on new clients
  int start_job_flag = 1;       // opaque job code, validated by the policy hook
  std::map<std::string, std::int64_t> client_choices;  // cosmetic picks (hair, ...): forwarded as Character::extra["client.<k>"]
};
struct MakeCharOutcome { bool ok = false; int error = 0; /* -1..-4 */ Character ch; };

// code is the wire answer of the reserved-delete flow (1 ok, 0 already queued, 2 party/guild/level,
// 3 not found / error, 4 party-guild-restricted request (request) or delay not elapsed (confirm), 5 bad code);
// `result` is the finer reason used to map the legacy 0x68 / 0x1fb answers.
enum class DeleteResult { Ok, AlreadyQueued, NotFound, Database, Party, Guild, BaseLevel, Time, BadCode };
struct DeleteOutcome { bool ok = false; int code = 0; Timestamp delete_date = 0; Timestamp remaining = 0; DeleteResult result = DeleteResult::NotFound; };
struct SelectOutcome { bool ok = false; int code = 0; std::optional<AuthTicket> ticket; Character ch; };
struct PinOutcome { bool ok = false; int attempts_left = 0; bool account_offlined = false; bool bad_format = false; };

class CharService {
 public:
  explicit CharService(GatewayDeps d);
  EnterOutcome enter(CharSession& s, AccountId aid, std::uint32_t id1, std::uint32_t id2, Sex sex);  // 025,026,019,023,024
  std::vector<Character> list(const CharSession& s) const;
  MakeCharOutcome make(const CharSession& s, const MakeCharRequest& r);           // 001..008
  DeleteOutcome request_delete(const CharSession& s, CharId id);                  // 009, 012
  // `option_mask`: 1 = e-mail, 2 = birthdate; 0 = use GatewayConfig::char_del_option.
  DeleteOutcome confirm_delete(const CharSession& s, CharId id, const std::string& code, int option_mask = 0);  // 010,011,012,013
  DeleteOutcome cancel_delete(const CharSession& s, CharId id);                   // 014
  SelectOutcome select(CharSession& s, int slot);                                 // 015, 016
  bool rename(const CharSession& s, CharId id, const std::string& new_name);      // 017
  int rename_code(const CharSession& s, CharId id, const std::string& new_name);  // 0 ok, 1 no counter, 4 exists, 5 guild, 6 party, 8 bad name
  bool move_slot(const CharSession& s, CharId id, int new_slot, bool swap_allowed);  // 018
  PinState pin_state(const Account& a, bool returning_from_world) const;          // 019
  PinOutcome pin_check(CharSession& s, const std::string& pin);                   // 021
  bool pin_set(CharSession& s, const std::string& old_pin, const std::string& new_pin);  // 020
  Timestamp ban_character(CharId id, Timestamp delta);                            // 016
  bool authenticate_from_world(AccountId aid, CharId id, std::uint32_t id1, const Sex* sex = nullptr);  // 031
  bool accept_save(CharId id, bool is_final, std::size_t payload, std::size_t expected);  // 032
  void startup_set_offline();                                                     // 028
  int cleanup_stale_online();                                                     // 029
  // World -> char select: the player comes back; the PIN does not have to be entered again (BR-CHAR-026).
  void prepare_return_from_world(AccountId aid, std::uint32_t id1, std::uint32_t id2, Sex sex);
  // The client dropped: free the directory slot held by this connection.
  void on_disconnect(const CharSession& s);
  int visible_slots(const Account& a) const;
  Timestamp now() const { return d_.clock.now(); }   // the service clock, for packet fields that depend on time
  // Packet-layer helpers.
  PinState pin_window_state(const CharSession& s) const;   // PIN window request: New without a PIN, else Ask
  bool pin_is_legal(const std::string& pin) const { return d_.policy.pin_is_legal(pin); }
  int job_flag_for_client_job(std::uint32_t job) const { return d_.policy.job_flag_for_client_job(job); }
  bool name_acceptable(const CharSession& s, CharId id, const std::string& name) const;  // CH_REQ_IS_VALID_CHARNAME
  // Slot move addressed by slot numbers, as the client sends it. Returns the legacy reason (0 ok, 1 failed)
  // and the remaining moves of the moved character.
  int move_slot_by_slot(const CharSession& s, int from, int to, int* moves_left);
 private:
  int slots_for(const Account& a) const;
  int check_name_code(const std::string& name, CharId except) const;  // 0 ok, -1 / -2 per BR-CHAR-001
  void audit(AccountId aid, CharId cid, const std::string& what);
  PinOutcome pin_fail(const Account& acc, CharSession& s);
  std::string pin_material(const std::string& pin) const;
  void arm_handoff_expiry(AccountId aid, CharId cid, std::uint32_t gen);
  GatewayDeps d_;
  std::string pepper_;
};

}  // namespace gateway
