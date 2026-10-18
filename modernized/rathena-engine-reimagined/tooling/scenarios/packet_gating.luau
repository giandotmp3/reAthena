--@rule BR-MAP-059 BR-MAP-060
--@mods rathena.base
--@map 200x200
-- Session gating and the connect packet checks, driven with raw frames (hex, little endian) so a
-- scenario can send what a well-behaved client never would. netproto's gating decides, not the sim.
-- BR-MAP-059: a packet is accepted only in the phase that matches its gate; anything else disconnects.
local early = sim.player { name = "early", connect = false }
early:send_raw("5f03000000")            -- CZ_REQUEST_MOVE (0x035f, 5 bytes): a well-formed in-world packet, sent before CZ_ENTER
sim.run_for(60)
sim.assert_eq(early:info().disconnected, true, "an in-world packet before the handshake disconnects")
sim.assert_true(string.find(early:info().reason, "not valid in this session phase", 1, true) ~= nil, "because of the session phase: " .. early:info().reason)

local unknown = sim.player { name = "unknown", connect = false }
unknown:send_raw("ff0f0000")            -- an opcode that is not in the table at all
sim.run_for(60)
sim.assert_eq(unknown:info().reason, "unknown opcode", "unknown opcode disconnects")

-- BR-MAP-060: the connect packet is checked before anything else. 0x0436, 23 bytes:
-- account id (u32) at 2, char id at 6, login id at 10, tick at 14, sex (u8) at 22.
local bad_account = sim.player { name = "bad_account", connect = false }
bad_account:send_raw("3604" .. "05000000" .. "05000000" .. "01000000" .. "00000000" .. "00000000" .. "01")
local refusal = bad_account:expect("refuse", 200)
sim.assert_eq(refusal.code, 2, "account id below the legal range: refusal code 2 (0x0074)")

local bad_sex = sim.player { name = "bad_sex", connect = false }
bad_sex:send_raw("3604" .. "c0c62d00" .. "c0c62d00" .. "01000000" .. "00000000" .. "00000000" .. "07")
sim.assert_eq(bad_sex:expect("refuse", 200).code, 6, "sex byte outside 0..1: refusal code 6")

-- a client that really connects can then use in-world packets
local ok = sim.player { name = "ok", x = 20, y = 20 }
ok:ping()
sim.assert_true(ok:wait_for("time", 100) ~= nil, "ZC_NOTIFY_TIME answers CZ_REQUEST_TIME in the world")
sim.assert_true(ok:connected())
