// API stubs. Each throws NotImplemented until its BR rule is implemented and
// the matching PENDING_TEST marker is removed.
#include "gateway/services.hpp"

namespace gateway {

#define STUB(rule) throw NotImplemented(rule)

LoginOutcome LoginService::login(const LoginRequest&) { STUB("BR-LOGIN-001 login pipeline"); }
void LoginService::on_logout(AccountId) { STUB("BR-LOGIN-034 logout/web token"); }
void LoginService::sweep_duplicate_clears() { STUB("BR-LOGIN-018 duplicate clear"); }

bool AccountAdmin::ban(AccountId, Timestamp) { STUB("BR-LOGIN-026"); }
void AccountAdmin::unban(AccountId) { STUB("BR-LOGIN-027"); }
void AccountAdmin::set_state(AccountId, int) { STUB("BR-LOGIN-028"); }
bool AccountAdmin::toggle_sex(AccountId) { STUB("BR-LOGIN-029"); }
bool AccountAdmin::change_email(AccountId, const std::string&, const std::string&) { STUB("BR-LOGIN-030"); }
int AccountAdmin::char_slots(AccountId) const { STUB("BR-LOGIN-031"); }
void AccountAdmin::apply_vip(AccountId, bool) { STUB("BR-LOGIN-032"); }
bool AccountAdmin::delete_account(AccountId) { STUB("BR-LOGIN-037"); }
bool AccountAdmin::gm_set_state(AccountId, AccountId, int) { STUB("BR-CHAR-055"); }
void AccountAdmin::record_pin_update(AccountId, const std::string&, Timestamp) { STUB("BR-LOGIN-035"); }

EnterOutcome CharService::enter(CharSession&, AccountId, std::uint32_t, std::uint32_t, Sex) { STUB("BR-CHAR-026"); }
std::vector<Character> CharService::list(const CharSession&) const { STUB("char list"); }
MakeCharOutcome CharService::make(const CharSession&, const MakeCharRequest&) { STUB("BR-CHAR-001..008"); }
DeleteOutcome CharService::request_delete(const CharSession&, CharId) { STUB("BR-CHAR-009"); }
DeleteOutcome CharService::confirm_delete(const CharSession&, CharId, const std::string&) { STUB("BR-CHAR-010"); }
DeleteOutcome CharService::cancel_delete(const CharSession&, CharId) { STUB("BR-CHAR-014"); }
SelectOutcome CharService::select(const CharSession&, int) { STUB("BR-CHAR-015"); }
bool CharService::rename(const CharSession&, CharId, const std::string&) { STUB("BR-CHAR-017"); }
bool CharService::move_slot(const CharSession&, CharId, int, bool) { STUB("BR-CHAR-018"); }
PinState CharService::pin_state(const Account&, bool) const { STUB("BR-CHAR-019"); }
PinOutcome CharService::pin_check(CharSession&, const std::string&) { STUB("BR-CHAR-021"); }
bool CharService::pin_set(CharSession&, const std::string&, const std::string&) { STUB("BR-CHAR-020"); }
Timestamp CharService::ban_character(CharId, Timestamp) { STUB("BR-CHAR-016"); }
bool CharService::authenticate_from_world(AccountId, CharId, std::uint32_t) { STUB("BR-CHAR-031"); }
bool CharService::accept_save(CharId, bool, std::size_t, std::size_t) { STUB("BR-CHAR-032"); }
void CharService::startup_set_offline() { STUB("BR-CHAR-028"); }
int CharService::cleanup_stale_online() { STUB("BR-CHAR-029"); }

}  // namespace gateway
