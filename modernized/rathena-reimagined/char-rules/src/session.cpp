#include "rathena/char_rules/session.hpp"

#include <algorithm>

namespace rathena::char_rules {
namespace {

bool constant_time_equals(std::string_view a, std::string_view b) {
	unsigned char diff = a.size() == b.size() ? 0 : 1;
	const std::size_t n = std::max(a.size(), b.size());
	for (std::size_t i = 0; i < n; ++i) {
		const unsigned char x = i < a.size() ? static_cast<unsigned char>(a[i]) : 0;
		const unsigned char y = i < b.size() ? static_cast<unsigned char>(b[i]) : 0;
		diff = static_cast<unsigned char>(diff | (x ^ y));
	}
	return diff == 0;
}

} // namespace

bool admit_to_char_server(int group_id, int online_users, const SessionConfig& cfg) {
	const bool restricted = cfg.max_connect_user == 0 || cfg.maintenance ||
	                        (cfg.max_connect_user > 0 && online_users >= cfg.max_connect_user);
	return !(restricted && group_id < cfg.gm_allow_group);
}

int clamp_char_slots(int slots) {
	if (slots > kMaxChars) return kMaxChars;
	if (slots == 0) return kMinChars;
	return slots;
}

DuplicateSessionAction on_duplicate_session(UnixTime now, const SessionConfig& cfg) {
	return {true, 8, now + cfg.duplicate_offline_after};
}

std::optional<AuthTicket> AuthTicketStore::consume_for_client(AccountId aid, std::uint32_t id1, std::uint32_t id2,
                                                              UnixTime now) {
	const auto it = tickets_.find(aid);
	if (it == tickets_.end()) return std::nullopt;
	if (expired(it->second, now)) {
		tickets_.erase(it);
		return std::nullopt;
	}
	if (it->second.login_id1 != id1 || it->second.login_id2 != id2) return std::nullopt;
	auto t = it->second;
	tickets_.erase(it);
	return t;
}

std::optional<AuthTicket> AuthTicketStore::consume_for_map(AccountId aid, CharId cid, std::uint32_t id1, Sex sex,
                                                           bool check_sex, UnixTime now) {
	const auto it = tickets_.find(aid);
	if (it == tickets_.end()) return std::nullopt;
	if (expired(it->second, now)) {
		tickets_.erase(it);
		return std::nullopt;
	}
	const auto& t = it->second;
	if (t.char_id != cid || t.login_id1 != id1 || (check_sex && t.sex != sex)) return std::nullopt;
	auto out = t;
	tickets_.erase(it);
	return out;
}

std::size_t AuthTicketStore::expire(UnixTime now) {
	return static_cast<std::size_t>(std::erase_if(tickets_, [&](const auto& kv) { return expired(kv.second, now); }));
}

CharAuthOutcome decide_char_auth(AuthTicketStore& store, AccountId aid, std::uint32_t id1, std::uint32_t id2,
                                 bool already_authed, bool login_connected, UnixTime now) {
	if (already_authed) return {CharAuthDecision::Reject, false};
	if (store.consume_for_client(aid, id1, id2, now)) return {CharAuthDecision::AcceptLocalTicket, true};
	if (login_connected) return {CharAuthDecision::AskLogin, false};
	return {CharAuthDecision::Reject, false};
}

void OnlineRegistry::dec(ServerId s) {
	if (s < 0) return;
	auto& c = counts_[s];
	if (c > 0) --c;
}

void OnlineRegistry::set_online(AccountId aid, CharId cid, ServerId server, int fd) {
	auto& e = online_[aid];
	if (e.account_id == aid && e.server >= 0 && e.server != server) dec(e.server);
	const bool was_counted = e.account_id == aid && e.server == server && server >= 0;
	e.account_id = aid;
	e.char_id = cid;
	e.server = server;
	e.fd = fd;
	if (server >= 0 && !was_counted) ++counts_[server];
}

void OnlineRegistry::set_offline(AccountId aid) {
	const auto it = online_.find(aid);
	if (it == online_.end()) return;
	dec(it->second.server);
	it->second.char_id = 0;
	it->second.server = kServerCharSelect;
	if (it->second.fd == -1) online_.erase(it);
}

void OnlineRegistry::mark_server_lost(ServerId server) {
	for (auto& [aid, e] : online_) {
		if (e.server == server) e.server = kServerUnknown;
	}
	counts_[server] = 0;
}

const OnlineEntry* OnlineRegistry::find(AccountId aid) const {
	const auto it = online_.find(aid);
	return it == online_.end() ? nullptr : &it->second;
}

int OnlineRegistry::user_count(ServerId server) const {
	const auto it = counts_.find(server);
	return it == counts_.end() ? 0 : std::max(0, it->second);
}

int OnlineRegistry::total_users() const {
	int n = 0;
	for (const auto& [s, c] : counts_) n += std::max(0, c);
	return n;
}

std::vector<AccountId> OnlineRegistry::cleanup_stale() {
	std::vector<AccountId> set_offline_ids;
	for (auto it = online_.begin(); it != online_.end();) {
		auto& e = it->second;
		if (e.fd != -1) { // still connected: skip and continue (legacy returned here)
			++it;
			continue;
		}
		if (e.server == kServerUnknown) {
			set_offline_ids.push_back(e.account_id);
			e.server = kServerCharSelect;
			e.char_id = 0;
		}
		if (e.server < 0) {
			it = online_.erase(it);
		} else {
			++it;
		}
	}
	return set_offline_ids;
}

MapRegistrationResult check_map_registration(std::string_view user, std::string_view pass,
                                             const InterServerCredentials& expected, bool free_slot) {
	if (expected.user.empty() || expected.pass.empty() || !free_slot) return MapRegistrationResult::Refused;
	const bool u = constant_time_equals(user, expected.user);
	const bool p = constant_time_equals(pass, expected.pass);
	return (u && p) ? MapRegistrationResult::Ok : MapRegistrationResult::Refused;
}

SaveDecision accept_save(const SaveRequest& req, const OnlineRegistry& online, std::size_t expected) {
	if (req.payload_size != expected) return SaveDecision::RejectSize;
	if (req.final_save || req.restore) return SaveDecision::Accept;
	const auto* e = online.find(req.account_id);
	if (e != nullptr && e->char_id == req.char_id && e->server == req.from_server) return SaveDecision::Accept;
	return SaveDecision::RejectNotOnline;
}

bool is_valid_account_id(std::uint32_t aid) { return aid >= kStartAccountNum && aid <= kEndAccountNum; }
bool is_valid_char_id(std::uint32_t cid) { return cid >= kStartCharNum; }
bool fits_name_field(std::string_view name) { return name.size() <= kNameLength - 1; }

std::optional<CharBanNotice> char_ban_notice(CharId cid, const CharBanOutcome& ban) {
	if (!ban.applied || !ban.kick) return std::nullopt;
	return CharBanNotice{0x2b14, cid, 2, static_cast<std::uint32_t>(ban.unban_time)};
}

} // namespace rathena::char_rules
