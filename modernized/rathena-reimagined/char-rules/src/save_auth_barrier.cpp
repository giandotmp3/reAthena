#include "rathena/char_rules/save_auth_barrier.hpp"

namespace rathena::char_rules {

void SaveAuthBarrier::save_received(CharId cid) { ++slots_[cid].in_flight; }

std::vector<PendingAuth> SaveAuthBarrier::finish(CharId cid, bool committed) {
	const auto it = slots_.find(cid);
	if (it == slots_.end() || it->second.in_flight == 0) {
		return {};
	}
	auto& s = it->second;
	--s.in_flight;
	std::vector<PendingAuth> out;
	if (!committed) {
		// A failed save means the DB does not hold the latest state: refuse everyone waiting.
		out.assign(s.waiting.begin(), s.waiting.end());
		s.waiting.clear();
	} else if (s.in_flight == 0) {
		out.assign(s.waiting.begin(), s.waiting.end());
		s.waiting.clear();
	}
	if (s.in_flight == 0 && s.waiting.empty()) {
		slots_.erase(it);
	}
	return out;
}

std::vector<PendingAuth> SaveAuthBarrier::save_committed(CharId cid) { return finish(cid, true); }
std::vector<PendingAuth> SaveAuthBarrier::save_failed(CharId cid) { return finish(cid, false); }

SaveAuthBarrier::AuthGate SaveAuthBarrier::auth_requested(const PendingAuth& req) {
	const auto it = slots_.find(req.char_id);
	if (it == slots_.end() || it->second.in_flight == 0) {
		return AuthGate::AnswerNow;
	}
	it->second.waiting.push_back(req);
	return AuthGate::Deferred;
}

std::size_t SaveAuthBarrier::saves_in_flight(CharId cid) const {
	const auto it = slots_.find(cid);
	return it == slots_.end() ? 0 : it->second.in_flight;
}

std::size_t SaveAuthBarrier::deferred_auths(CharId cid) const {
	const auto it = slots_.find(cid);
	return it == slots_.end() ? 0 : it->second.waiting.size();
}

} // namespace rathena::char_rules
