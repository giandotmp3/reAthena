--!nonstrict
-- rathena-sim scenario prelude: the Luau side of the `sim` global.
--
-- A scenario runs as a coroutine in its own sandboxed script-host state (mod id "sim.scenario": no
-- storage, no RNG, no permissions). It cannot call C++ directly. Every `sim.*` call is an RPC to the
-- host over the dialogue channel: the call is encoded as text and sent with input(prompt); the host
-- runs it against the Sim and answers with the encoded result (see include/tooling/sim_value.hpp).
-- Requests and replies longer than one dialogue string (4096 bytes) are split into pieces:
--   request:  "C<piece>" (more follows, answered "K") ... "X<last piece>" (the host runs it)
--   reply:    "<R|E><0|1><piece>"; 1 = more pieces follow, fetched with "M"
-- wait(ms) in a scenario is the same as sim.run_for(ms): the host advances virtual time.

local MAXPIECE = 3000

local function enc(v, out)
	local t = type(v)
	if v == nil then
		out[#out + 1] = "N"
	elseif t == "boolean" then
		out[#out + 1] = v and "T" or "F"
	elseif t == "number" then
		out[#out + 1] = "n" .. string.format("%.17g", v) .. ";"
	elseif t == "string" then
		out[#out + 1] = "s" .. #v .. ":" .. v
	elseif t == "table" then
		local n, count = #v, 0
		for _ in pairs(v) do count += 1 end
		if n == count then
			out[#out + 1] = "a" .. n .. ":"
			for i = 1, n do enc(v[i], out) end
		else
			local keys = {}
			for k in pairs(v) do keys[#keys + 1] = k end
			table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
			out[#out + 1] = "m" .. count .. ":"
			for _, k in ipairs(keys) do
				local ks = tostring(k)
				out[#out + 1] = "s" .. #ks .. ":" .. ks
				enc(v[k], out)
			end
		end
	else
		error("sim: cannot send a " .. t .. " to the host", 0)
	end
end

local function dec(s, i)
	local c = string.sub(s, i, i)
	if c == "N" then return nil, i + 1 end
	if c == "T" then return true, i + 1 end
	if c == "F" then return false, i + 1 end
	if c == "n" then
		local j = string.find(s, ";", i, true)
		return tonumber(string.sub(s, i + 1, j - 1)), j + 1
	end
	if c == "s" then
		local j = string.find(s, ":", i, true)
		local len = tonumber(string.sub(s, i + 1, j - 1))
		return string.sub(s, j + 1, j + len), j + len + 1
	end
	if c == "a" then
		local j = string.find(s, ":", i, true)
		local n = tonumber(string.sub(s, i + 1, j - 1))
		local t, p = {}, j + 1
		for k = 1, n do
			local v
			v, p = dec(s, p)
			t[k] = v
		end
		return t, p
	end
	if c == "m" then
		local j = string.find(s, ":", i, true)
		local n = tonumber(string.sub(s, i + 1, j - 1))
		local t, p = {}, j + 1
		for _ = 1, n do
			local k, v
			k, p = dec(s, p)
			v, p = dec(s, p)
			t[k] = v
		end
		return t, p
	end
	error("sim: malformed reply from the host at " .. i .. " ('" .. c .. "')", 0)
end

local function rpc(cmd, ...)
	local n = select("#", ...)
	local out = { "a" .. (n + 1) .. ":", "s" .. #cmd .. ":" .. cmd }
	for i = 1, n do enc((select(i, ...)), out) end
	local payload = table.concat(out)
	local pos, total = 1, #payload
	while total - pos + 1 > MAXPIECE do
		input("C" .. string.sub(payload, pos, pos + MAXPIECE - 1))
		pos += MAXPIECE
	end
	local reply = input("X" .. string.sub(payload, pos))
	local kind = string.sub(reply, 1, 1)
	local parts = {}
	while true do
		parts[#parts + 1] = string.sub(reply, 3)
		if string.sub(reply, 2, 2) ~= "1" then break end
		reply = input("M")
	end
	local body = table.concat(parts)
	if kind == "E" then error("sim." .. cmd .. ": " .. body, 3) end
	return (dec(body, 1))
end

sim = {}

-- ---- inspection helpers -------------------------------------------------------------------------
local function dump(v, depth)
	depth = depth or 0
	local t = type(v)
	if t == "string" then return string.format("%q", v) end
	if t ~= "table" then return tostring(v) end
	if depth > 4 then return "{...}" end
	local keys = {}
	for k in pairs(v) do keys[#keys + 1] = k end
	table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
	local parts = {}
	for _, k in ipairs(keys) do
		parts[#parts + 1] = (type(k) == "number" and "" or (tostring(k) .. "=")) .. dump(v[k], depth + 1)
	end
	return "{" .. table.concat(parts, ", ") .. "}"
end
sim.dump = dump

-- A Luau source literal for a plain value (numbers, strings, booleans, nested tables). Used to hand
-- combat snapshots to the ruleset package, which parses them in its own state.
local function lua_literal(v)
	local t = type(v)
	if t == "number" then return string.format("%.17g", v) end
	if t == "string" then return string.format("%q", v) end
	if t == "boolean" then return tostring(v) end
	if t == "table" then
		local keys = {}
		for k in pairs(v) do keys[#keys + 1] = k end
		table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
		local parts = {}
		for _, k in ipairs(keys) do
			parts[#parts + 1] = "[" .. lua_literal(k) .. "]=" .. lua_literal(v[k])
		end
		return "{" .. table.concat(parts, ",") .. "}"
	end
	error("sim.lua: cannot serialise a " .. t, 2)
end
sim.lua = lua_literal

-- Luau source that defines `lit(value)` (a source literal of a plain table); prefix it to an eval() chunk
-- that has to hand a table of the mod's state back to the scenario as text.
sim.LIT_SRC = [==[
local function lit(v)
	local t = type(v)
	if t == "number" then return string.format("%.17g", v) end
	if t == "string" then return string.format("%q", v) end
	if t == "boolean" then return tostring(v) end
	local keys = {}
	for k in pairs(v) do keys[#keys + 1] = k end
	table.sort(keys, function(a, b) return tostring(a) < tostring(b) end)
	local parts = {}
	for _, k in ipairs(keys) do parts[#parts + 1] = "[" .. lit(k) .. "]=" .. lit(v[k]) end
	return "{" .. table.concat(parts, ",") .. "}"
end
]==]

-- Splits "a<TAB>b<TAB>c" lines printed by an eval() chunk: returns the fields of the lines that start
-- with `tag` (without the tag).
function sim.records(lines, tag)
	local out = {}
	for _, l in ipairs(lines) do
		local f = string.split(l, "\t")
		if f[1] == tag then
			table.remove(f, 1)
			out[#out + 1] = f
		end
	end
	return out
end

-- ---- time, seed, tags ----------------------------------------------------------------------------------
function sim.now() return rpc("now") end
function sim.step(n) rpc("step", n or 1) end
function sim.run_for(ms) rpc("run_for", ms) end
function sim.run_until(pred, timeout_ms, step_ms)
	local spent, dt = 0, step_ms or 20
	while spent < timeout_ms do
		if pred() then return true end
		rpc("run_for", dt)
		spent += dt
	end
	return pred()
end
function sim.seed() return rpc("seed") end
function sim.rule(...)
	for _, id in ipairs({ ... }) do rpc("tag", id) end
end
function sim.mods() return rpc("mods") end

-- ---- assertions ----------------------------------------------------------------------------------------------
local function counted() rpc("assert") end

function sim.fail(msg) error(msg or "scenario failed", 2) end
function sim.assert_true(cond, msg)
	if not cond then error("assertion failed: " .. (msg or "condition is false"), 2) end
	counted()
end
function sim.assert_eq(actual, expected, msg)
	if actual ~= expected then
		error(string.format("assertion failed: %s: expected %s, got %s", msg or "values differ", dump(expected), dump(actual)), 2)
	end
	counted()
end
function sim.assert_ne(actual, unexpected, msg)
	if actual == unexpected then
		error(string.format("assertion failed: %s: value must not be %s", msg or "values equal", dump(unexpected)), 2)
	end
	counted()
end
function sim.assert_near(actual, expected, eps, msg)
	if math.abs(actual - expected) > eps then
		error(string.format("assertion failed: %s: expected %s +- %s, got %s", msg or "not near", tostring(expected), tostring(eps), tostring(actual)), 2)
	end
	counted()
end
function sim.assert_fails(fn, pattern, msg)
	local ok, err = pcall(fn)
	if ok then error("assertion failed: " .. (msg or "the call should have failed"), 2) end
	if pattern and not string.find(tostring(err), pattern, 1, true) then
		error(string.format("assertion failed: %s: error %s does not contain %q", msg or "wrong error", dump(tostring(err)), pattern), 2)
	end
	counted()
end
function sim.try(fn, ...) return pcall(fn, ...) end

-- ---- players ----------------------------------------------------------------------------------------------------
local Player = {}
Player.__index = Player

local function wrap_player(r)
	return setmetatable({ id = r.id, gid = r.gid, aid = r.aid, name = r.name }, Player)
end

-- sim.player{ name, x, y, job, level, unit = <combat snapshot table>, connect = true }
function sim.player(spec)
	spec = spec or {}
	local unit = spec.unit
	if type(unit) == "table" then unit = lua_literal(unit) end
	return wrap_player(rpc("player_new", { aid = spec.aid, name = spec.name, x = spec.x, y = spec.y, job = spec.job, level = spec.level, unit = unit, connect = spec.connect }))
end

-- sim.crowd{ count, prefix, x0, y0, columns, spacing } connects count players in the same ticks.
function sim.crowd(spec)
	local ids = rpc("crowd_new", spec)
	local out = {}
	for i, r in ipairs(ids) do out[i] = wrap_player(r) end
	return out
end

function Player:walk(x, y) rpc("p_walk", self.id, x, y) end
function Player:say(text) rpc("p_say", self.id, text) end
-- Sends the text exactly as given, without the "<name> : " prefix (to forge or break the speaker name).
function Player:say_raw(text) rpc("p_say_raw", self.id, text) end
function Player:click_npc(name) rpc("p_click_npc", self.id, name) end
function Player:answer(n) rpc("p_answer", self.id, n) end
function Player:next() rpc("p_next", self.id) end
function Player:input(text)
	if type(text) == "number" then rpc("p_input_number", self.id, text) else rpc("p_input", self.id, text) end
end
function Player:close_dialog() rpc("p_close_dialog", self.id) end
function Player:ping() rpc("p_ping", self.id) end
function Player:attack(target) rpc("p_attack", self.id, type(target) == "table" and target.gid or target) end
function Player:cast(skill, target, level) rpc("p_cast", self.id, skill, type(target) == "table" and target.gid or target, level or 1) end
function Player:disconnect() rpc("p_disconnect", self.id) end
function Player:send_raw(hex) rpc("p_send_raw", self.id, hex) end

-- Waits (in virtual time) for an unconsumed event; returns the event table or nil on timeout.
-- Names: packet names (ZC_SAY_DIALOG) or aliases (say, next, menu, input, close, move, stop, appear,
-- standing, moving, vanish, chat, chat_self, damage, accept, refuse, time).
function Player:wait_for(event, timeout_ms) return rpc("p_wait_for", self.id, event, timeout_ms or 1000) end
function Player:expect(event, timeout_ms)
	local e = rpc("p_expect", self.id, event, timeout_ms or 1000)
	counted()
	return e
end
function Player:expect_none(event, within_ms)
	local none = rpc("p_expect_none", self.id, event, within_ms or 200)
	if not none then error(self.name .. ": unexpected event '" .. event .. "'", 2) end
	counted()
end
-- Waits for the unit's OWN ZC_STOPMOVE (others in view broadcast theirs to us too).
function Player:wait_stop(timeout_ms)
	local deadline = sim.now() + timeout_ms
	while true do
		local left = deadline - sim.now()
		if left <= 0 then return nil end
		local e = self:wait_for("stop", left)
		if not e then return nil end
		if e.AID == self.gid then return e end
	end
end
function Player:expect_stop(timeout_ms)
	local e = self:wait_stop(timeout_ms or 5000)
	if not e then error(self.name .. ": no ZC_STOPMOVE for itself within " .. (timeout_ms or 5000) .. " ms", 2) end
	counted()
	return e
end
function Player:events() return rpc("p_events", self.id) end
function Player:history(event) return rpc("p_history", self.id, event) end
function Player:count(event) return rpc("p_count", self.id, event) end
function Player:info() return rpc("p_info", self.id) end
function Player:pos()
	local i = rpc("p_info", self.id)
	return i.x, i.y
end
function Player:sees(other) return rpc("p_sees", self.id, type(other) == "table" and other.gid or other) end
function Player:connected() return rpc("p_info", self.id).connected end

function sim.assert_pos(p, x, y)
	local px, py = p:pos()
	if px ~= x or py ~= y then error(string.format("assertion failed: %s is at (%s,%s), expected (%s,%s)", p.name, tostring(px), tostring(py), tostring(x), tostring(y)), 2) end
	counted()
end
function sim.assert_sees(a, b)
	if not a:sees(b) then error(a.name .. " should see " .. b.name, 2) end
	counted()
end
function sim.assert_blind(a, b)
	if a:sees(b) then error(a.name .. " should not see " .. b.name, 2) end
	counted()
end

-- ---- npcs, dummies, combat -------------------------------------------------------------------------------------
function sim.npc_load(path) return rpc("npc_load", path) end
function sim.npc_place(name, x, y) return rpc("npc_place", name, x, y) end
function sim.dummy(spec)
	local unit = spec.unit
	if type(unit) == "table" then unit = lua_literal(unit) end
	return rpc("dummy", spec.name or "dummy", spec.x, spec.y, unit)
end
function sim.set_unit(gid, unit)
	if type(gid) == "table" then gid = gid.gid end
	rpc("unit_set", gid, type(unit) == "table" and lua_literal(unit) or unit)
end
-- Direct call of the loaded ruleset's basic attack between two units (gids or players).
function sim.attack(a, b)
	return rpc("attack", type(a) == "table" and a.gid or a, type(b) == "table" and b.gid or b)
end
function sim.last_damage(gid)
	return rpc("last_damage", type(gid) == "table" and gid.gid or gid)
end
function sim.dialogue_of(p) return rpc("dialogue_of", type(p) == "table" and p.gid or p) end
function sim.npc_log() return rpc("npc_log") end
function sim.block(x, y) rpc("block", x, y) end

-- ---- mods, hooks, reload, storage ----------------------------------------------------------------------------------
-- Prints of a chunk run inside the mod's own state; returns the lines. The package's modules are
-- reachable as SIM_IMPORT("scripts/...") inside the chunk.
function sim.eval(mod, src) return rpc("eval", mod, src) end
function sim.reload(mod, init_source) return rpc("reload", mod, init_source) end
function sim.generation(mod) return rpc("generation", mod) end
function sim.dispatch(event, value) return rpc("dispatch", event, value or 0) end
function sim.registrations(mod, generation) return rpc("registrations", mod, generation) end
function sim.disable_mod(id) rpc("mod_disable", id) end
function sim.enable_mod(id) rpc("mod_enable", id) end
function sim.mod_state(id) return rpc("mod_state", id) end
function sim.storage(scope, owner, mod, key) return rpc("storage_get", scope, tostring(owner), mod, key) end
function sim.rng() return rpc("rng") end
function sim.trace_hash() return rpc("trace_hash") end
function sim.stats() return rpc("stats") end
