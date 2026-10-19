--!nonstrict
-- Statuses that appear in the legacy code only through their conditional restrictions (status_calc_state, status.cpp:5592-5830): the rest of their
-- record (states, flags) is data. The conditions of the other conditional statuses are in their definitions (`state_when`).
local status = import("scripts/status/status")
local define = status.define

define("nochat", { -- manner / muted player: val1 is a bit set (MANNER_NOCHAT = no chat, MANNER_NOITEM = no pick-up / drop / consume)
	fields = { val1 = "manner" },
	state_when = {
		nochat = "(manner & MANNER_NOCHAT) != 0", nopickitem = "(manner & MANNER_NOITEM) != 0",
		nodropitem = "(manner & MANNER_NOITEM) != 0", noconsumeitem = "(manner & MANNER_NOITEM) != 0",
	},
})

define("winkcharm", { state_when = { nocast = "u.is_pc == 0" } })

define("oblivioncurse", {
	fields = { val3 = "silence" },
	state_when = { nocast = "silence == 1" },
})

define("crystalize", { state_when = { nomove = "u.is_mob == 0" } })
