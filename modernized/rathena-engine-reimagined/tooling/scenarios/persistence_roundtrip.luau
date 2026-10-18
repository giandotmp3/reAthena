--@rule TL-SIM-030
--@mods rathena.base
--@map 200x200
-- Persistence through the sim: the last position is written to the mod KV store on logout (SQLite, in
-- memory, behind script-host's StorageApi) and read back when the same account connects again.
sim.assert_eq(sim.stats().storage_backend, "sqlite", "the SQLite backend of the persistence unit is in use")
local first = sim.player { name = "neo", x = 100, y = 100 }
first:walk(110, 100)
first:expect_stop(3000)
first:disconnect()
sim.run_for(50)
local saved = sim.storage("character", first.aid, "sim.world", "pos")
sim.assert_eq(saved.x, 110, "saved x")
sim.assert_eq(saved.y, 100, "saved y")

-- the same account logs in again, asking to spawn somewhere else: the saved position wins
local again = sim.player { name = "neo", aid = first.aid, x = 50, y = 50 }
sim.assert_pos(again, 110, 100)
sim.assert_eq(again.aid, first.aid)
