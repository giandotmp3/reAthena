#include "rathena/char_rules/gm.hpp"

namespace rathena::char_rules {

GmOutcome process_gm_request(const GmRequest& req, const GmContext& ctx) {
	GmOutcome out;
	const bool from_server = req.requester_aid == kServerRequester;
	out.answer_requester = !from_server;

	if (!ctx.target) {
		out.result = GmResult::NotFound;
		return out;
	}
	// Authority re-check (defect fix): known requester, permitted op, and not outranked by target.
	if (!from_server) {
		if (!ctx.requester_group_level || !ctx.requester_may || !ctx.requester_may(req.op) ||
		    *ctx.requester_group_level < ctx.target->group_level) {
			out.result = GmResult::NotAuthorized;
			return out;
		}
	}
	if (!ctx.login_connected) {
		out.result = GmResult::LoginOffline;
		return out;
	}

	const auto& t = *ctx.target;
	switch (req.op) {
	case GmOp::LoginBlock: out.forward = GmForward{0x2724, t.account_id, t.char_id, 5}; break;
	case GmOp::LoginBan: out.forward = GmForward{0x2725, t.account_id, t.char_id, req.timediff}; break;
	case GmOp::LoginUnblock: out.forward = GmForward{0x2724, t.account_id, t.char_id, 0}; break;
	case GmOp::LoginUnban: out.forward = GmForward{0x272a, t.account_id, t.char_id, 0}; break;
	case GmOp::LoginChangeSex:
		out.forward = GmForward{0x2727, t.account_id, t.char_id, 0};
		out.answer_requester = false;
		break;
	case GmOp::LoginVip:
		out.forward = GmForward{0x2742, t.account_id, t.char_id, req.timediff};
		out.answer_requester = out.answer_requester && (req.val1 & 4) != 0;
		break;
	case GmOp::ChangeCharSex:
		out.forward = GmForward{0, t.account_id, t.char_id, static_cast<std::int32_t>(req.sex)};
		out.answer_requester = false;
		break;
	case GmOp::Ban:
	case GmOp::Unban:
		out.forward = GmForward{0, t.account_id, t.char_id, req.timediff}; // char ban: BR-CHAR-016
		break;
	}
	out.result = GmResult::Done;
	return out;
}

} // namespace rathena::char_rules
