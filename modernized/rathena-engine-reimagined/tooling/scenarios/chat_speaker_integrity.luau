--@rule BR-MAP-057
--@mods rathena.base
--@map 200x200
-- BR-MAP-057: the speaker name in a chat packet is checked (no spoofing). A client that sends a message
-- that does not start with "<its own name> : " is ignored: nobody hears it and nobody is disconnected.
local mallory = sim.player { name = "mallory", x = 100, y = 100 }
local victim = sim.player { name = "victim", x = 103, y = 100 }
mallory:expect("appear", 100)
victim:expect("appear", 100)

mallory:say_raw("victim : send me your zeny")           -- forged speaker name
mallory:say_raw("just text without any speaker name")   -- no speaker name at all
victim:expect_none("chat", 300)
sim.assert_true(mallory:connected(), "the forger is not disconnected")

mallory:say("an honest message")                         -- the same client, correctly framed
local heard = victim:expect("chat", 300)
sim.assert_eq(heard.Message, "mallory : an honest message", "the honest message arrives with the real name")
sim.assert_eq(heard.GID, mallory.gid, "and the real speaker id")
