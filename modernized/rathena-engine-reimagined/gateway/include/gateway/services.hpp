#pragma once
// Service API stubs matching the interface contracts (client login/char
// protocol semantics, spec section 3). Every method whose behavior-contract
// rule is not yet implemented throws NotImplemented; the acceptance tests tagged
// with the rule ID are expected-failure until then.
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
};

// ---- Login (CAP-01..04) ---------------------------------------------------
class LoginService {
 public:
  explicit LoginService(GatewayDeps d) : d_(d) {}
  // BR-LOGIN-001..012, 015, 016, 017, 018, 019 (issue), 022, 023, 034, 039; lazy rehash.
  LoginOutcome login(const LoginRequest& req);
  // Called on logout/disconnect: clears online entry, schedules web-token disable (BR-LOGIN-034).
  void on_logout(AccountId aid);
  // BR-LOGIN-018: force-clears a stale online entry after dup_login_force_clear_s.
  void sweep_duplicate_clears();
 private:
  GatewayDeps d_;
};

// ---- Account administration (CAP-02) -------------------------------------
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
  // BR-CHAR-055: authority is re-checked here, not trusted from the caller.
  bool gm_set_state(AccountId requester, AccountId target, int state);
  // BR-LOGIN-035
  void record_pin_update(AccountId aid, const std::string& pin_hash, Timestamp when);
 private:
  GatewayDeps d_;
};

// ---- Character plumbing (CAP-05, 06) ---------------------------------------
enum class PinState { Ok, New, Passed, Ask, Expired };

struct CharSession {
  AccountId account_id = 0;
  std::uint32_t login_id1 = 0, login_id2 = 0;
  Sex sex = Sex::Male;
  std::string ip;
  rules::SessionPhase phase = rules::SessionPhase::Unauthenticated;
  bool pin_verified = false;
};

struct EnterOutcome { bool ok = false; int refusal_code = 0; PinState pin = PinState::Ok; int slots = 0; };

struct MakeCharRequest {
  std::string name;
  int slot = 0;
  rules::StartStats stats{1, 1, 1, 1, 1, 1};
  bool new_client = true;       // PACKETVER >= 20151001
  Sex sex = Sex::Male;          // client-chosen on new clients
  int start_job_flag = 1;       // 1 = Novice, 2 = Summoner
};
struct MakeCharOutcome { bool ok = false; int error = 0; /* -1..-4 */ Character ch; };

struct DeleteOutcome { bool ok = false; int code = 0; Timestamp delete_date = 0; };
struct SelectOutcome { bool ok = false; int code = 0; std::optional<AuthTicket> ticket; };
struct PinOutcome { bool ok = false; int attempts_left = 0; bool account_offlined = false; };

class CharService {
 public:
  explicit CharService(GatewayDeps d) : d_(d) {}
  EnterOutcome enter(CharSession& s, AccountId aid, std::uint32_t id1, std::uint32_t id2, Sex sex);  // 025,026,019,023,024
  std::vector<Character> list(const CharSession& s) const;
  MakeCharOutcome make(const CharSession& s, const MakeCharRequest& r);           // 001..008
  DeleteOutcome request_delete(const CharSession& s, CharId id);                  // 009, 012
  DeleteOutcome confirm_delete(const CharSession& s, CharId id, const std::string& yymmdd);  // 010,012,013
  DeleteOutcome cancel_delete(const CharSession& s, CharId id);                   // 014
  SelectOutcome select(const CharSession& s, int slot);                           // 015, 016
  bool rename(const CharSession& s, CharId id, const std::string& new_name);      // 017
  bool move_slot(const CharSession& s, CharId id, int new_slot, bool swap_allowed);  // 018
  PinState pin_state(const Account& a, bool returning_from_world) const;          // 019
  PinOutcome pin_check(CharSession& s, const std::string& pin);                   // 021
  bool pin_set(CharSession& s, const std::string& old_pin, const std::string& new_pin);  // 020
  Timestamp ban_character(CharId id, Timestamp delta);                            // 016
  bool authenticate_from_world(AccountId aid, CharId id, std::uint32_t id1);      // 031
  bool accept_save(CharId id, bool is_final, std::size_t payload, std::size_t expected);  // 032
  void startup_set_offline();                                                     // 028
  int cleanup_stale_online();                                                     // 029
 private:
  GatewayDeps d_;
};

}  // namespace gateway
