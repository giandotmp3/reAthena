--@rule TL-SIM-011
--@mods rathena.base
--@map 200x200
-- Two players walk towards each other through engine-core movement; the area broadcast makes them
-- appear to one another when they come into view, and vanish when one walks away again.
local alice = sim.player { name = "alice", x = 100, y = 100 }
local bob = sim.player { name = "bob", x = 140, y = 100 }   -- 40 cells apart: far outside the 14-cell view
sim.assert_blind(alice, bob)
sim.assert_blind(bob, alice)

alice:walk(120, 100)                     -- 20 cells: 3 s at 150 ms per cell
bob:walk(121, 100)
alice:expect_stop(4000)
bob:expect_stop(4000)
sim.assert_pos(alice, 120, 100)
sim.assert_pos(bob, 121, 100)

-- they came into view while walking: both were told about the other
local ev = alice:expect("appear", 10)
sim.assert_eq(ev.name == "ZC_NOTIFY_STANDENTRY11" or ev.name == "ZC_NOTIFY_MOVEENTRY11", true, "appear packet")
sim.assert_eq(ev.gid, bob.gid, "alice was told about bob")
sim.assert_sees(alice, bob)
sim.assert_sees(bob, alice)

-- chat reaches the other player in view, with the speaker's name
alice:say("hello bob")
local heard = bob:expect("chat", 200)
sim.assert_eq(heard.text, "alice : hello bob", "chat text")
sim.assert_eq(heard.gid, alice.gid, "chat speaker")

-- bob walks out of view
bob:walk(145, 100)
alice:expect("vanish", 3000)
sim.assert_blind(alice, bob)
