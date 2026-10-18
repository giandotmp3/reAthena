--!nonstrict
-- usage: luau run_worst.luau -a <npc path> [set] [runBudget]
local R = require("./npcrun")
local C = require("./corpus")
local path = ({ ... })[1]
local setname = ({ ... })[2] or "re"
local budget = tonumber(({ ... })[3]) or 300
local ext = ({ ... })[4] == "ext"
print("command set:", ext and "base + ext" or "base only")
local uf = C.userfuncs()
local t0 = os.clock()
local funcs, nf, ff = R.loadFuncs(setname, uf)
print(string.format("function registry (%s set): %d lowered, %d failed, %.3fs", setname, nf, ff, os.clock() - t0))
local res = R.runFile(path, { uf = uf, funcs = funcs, runBudget = budget, profiles = { "poor", "rich" }, timeBudget = 20, ext = ext, flipBudget = tonumber(({ ... })[5]) or 150 })
print(R.report(res, { profiles = { 1, 2 } }))
