// Per-char_id load-after-save barrier (architecture §3 char-server row; §9 M2; phase-3 exit
// criterion "save->auth barrier test passes").
//
// Contract: a character save (chrif 0x2b01) that has been received for char_id C is committed
// before any auth request for C (chrif 0x2b26) is answered. Auth requests that arrive while a
// save for C is in flight are deferred, then released in arrival order when the last in-flight
// save for C commits. If a save fails, deferred auths for C are refused (never answered with a
// stale load). Other char_ids are never blocked.
//
// The barrier is pure bookkeeping: the char-server loop calls it around its synchronous SQL.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

#include "rathena/char_rules/domain.hpp"

namespace rathena::char_rules {

struct PendingAuth {
	AccountId account_id = 0;
	CharId char_id = 0;
	std::uint32_t login_id1 = 0;
	ServerId from_server = 0;
	std::uint64_t request_seq = 0; ///< caller-assigned arrival order
	friend bool operator==(const PendingAuth&, const PendingAuth&) = default;
};

class SaveAuthBarrier {
public:
	enum class AuthGate : std::uint8_t { AnswerNow, Deferred };

	/// 0x2b01 accepted for persistence (BR-CHAR-032 passed).
	void save_received(CharId cid);
	/// The save transaction for `cid` committed. Returns auths that may now be answered.
	std::vector<PendingAuth> save_committed(CharId cid);
	/// The save transaction failed. Returns deferred auths that must be refused.
	std::vector<PendingAuth> save_failed(CharId cid);

	/// 0x2b26 arrived. AnswerNow: no save in flight for this char_id.
	AuthGate auth_requested(const PendingAuth& req);

	std::size_t saves_in_flight(CharId cid) const;
	std::size_t deferred_auths(CharId cid) const;

private:
	struct Slot {
		std::size_t in_flight = 0;
		std::deque<PendingAuth> waiting;
	};
	std::vector<PendingAuth> finish(CharId cid, bool committed);
	std::unordered_map<CharId, Slot> slots_;
};

} // namespace rathena::char_rules
