--!nonstrict
-- Lazy corpus loader (architecture section 7, "Changes required by M0(b)" item 2): the eager prototype parsed and kept the
-- AST of every body (250 to 300 MB for the renewal set). This loader
--   * indexes a file once with the declaration splitter (declsplit.luau, noBody): per NPC it keeps header fields and the byte range of the
--     body, plus the event labels found by a text scan; nothing is parsed;
--   * parses + lowers one NPC (or `function script`) the first time something needs its program, keeps only the flat op list (the
--     statement AST is dropped; expression nodes referenced by the ops are the only tree left) and caches it on the entry;
--   * shares one program and one `.` variable table between a template and all of its `duplicate` clones (open question Q-H, see below).
--
-- Q-H (are `.` NPC variables shared by duplicate() clones?): YES. In the legacy server `.` variables live in `script_code::local`
-- (script.cpp:2658 initialises `code->local.vars`; script.cpp:2734, :2800, :3054, :3163 and :3257 read and write `st->script->local`),
-- and `npc_parse_duplicate` makes the clone point at the template's code object (`nd->u.scr.script = dnd->u.scr.script`,
-- npc.cpp:4602), so every clone sees the same `local` registry. `varsOf` below hands the same table to the whole chain.
local Declsplit = require("./declsplit")
local Parser = require("./parser")
local Lower = require("./lower")

local sub, find, lower, byte = string.sub, string.find, string.lower, string.byte

local Loader = {}
Loader.__index = Loader

-- opts.builtins: argument-spec table (data builtins.luau); opts.readText(path) -> string: optional provider used when a file was added
-- without its text (the text is then re-read for every parse and never retained)
function Loader.new(opts)
	opts = opts or {}
	return setmetatable({
		builtins = opts.builtins or require("./builtins"),
		readText = opts.readText,
		files = {},      -- path -> { path, text|nil, order }
		fileOrder = {},
		npcs = {},       -- exname -> entry (scripts and duplicates, in load order via npcList)
		npcsLower = {},  -- lower(exname) -> entry (event names are matched case-insensitively, npc.cpp:1273)
		npcList = {},
		funcs = {},      -- lower function name -> entry
		uf = {},         -- lower function name -> true (parser input: `Name(args)` call syntax)
		byLabel = {},    -- lower label -> array of entries (event labels only)
		stats = { files = 0, scripts = 0, duplicates = 0, functions = 0, warps = 0, shops = 0, monsters = 0, mapflags = 0, errors = 0,
			parsed = 0, parseFailed = 0 },
		diagnostics = {},
	}, Loader)
end

-- exname: "Display::Unique" -> "Unique"; a name without "::" is its own unique name (npc.cpp npc_parse_script)
local function exnameOf(name: string): string
	local _, e = find(name, "::", 1, true)
	if e then return sub(name, e + 1) end
	return name
end

local function parsePlacement(w1: string)
	local map, x, y, dir = string.match(w1, "^([^,]+),(%-?%d+),(%-?%d+),?(%-?%d*)")
	if not map then return w1 == "-" and "" or w1, 0, 0, 0 end
	return map, tonumber(x), tonumber(y), tonumber(dir) or 0
end

-- event labels of a body, by text scan: a label is `On...:` at the start of a statement. The result is only used to register event
-- hooks before anything is parsed; once an NPC is lowered its real label table (prog.labels) takes over.
local function scanEvents(text: string, from: number, to: number): { string }?
	local body = sub(text, from, to)
	if not find(body, "[Oo][Nn]", 1) then return nil end
	-- labels inside /* ... */ are not labels (real scripts keep dead event handlers there)
	if find(body, "/*", 1, true) then body = string.gsub(body, "/%*.-%*/", "") end
	local out, seen
	local init = 1
	while true do
		local s, e, label = find(body, "[\n;{}][ \t]*([Oo][Nn][%w_]*)[ \t]*:", init)
		if not s then break end
		init = e + 1
		-- `On...::` would be a scope operator, not a label
		if byte(body, e + 1) ~= 58 then
			seen = seen or {}
			local l = lower(label)
			if not seen[l] then
				seen[l] = true
				out = out or {}
				out[#out + 1] = label
			end
		end
	end
	-- a label at the very start of the body (no preceding separator)
	local s, e, label = find(body, "^[ \t]*([Oo][Nn][%w_]*)[ \t]*:")
	if s and byte(body, e + 1) ~= 58 and not (seen and seen[lower(label)]) then
		out = out or {}
		table.insert(out, 1, label)
	end
	return out
end

-- Indexes one file. text may be nil when a readText provider is configured (then the file is scanned from the provider's text and
-- the text is not kept). Returns number of NPC entries added, and a list of diagnostics.
function Loader:addFile(path: string, text: string?)
	local keep = text ~= nil
	local src = text
	if not src then
		assert(self.readText, "addFile without text needs a readText provider")
		src = self.readText(path)
	end
	local file = { path = path, text = keep and text or nil, order = #self.fileOrder + 1 }
	self.files[path] = file
	self.fileOrder[#self.fileOrder + 1] = file
	self.stats.files += 1
	local r = Declsplit.split(src, { noBody = true })
	local added = 0
	for _, e in r.errors do
		self.stats.errors += 1
		self.diagnostics[#self.diagnostics + 1] = { file = path, line = Declsplit.lineof(src, e[2]), message = e[1] }
	end
	local st = self.stats
	st.warps += r.counts.warp or 0
	st.shops += r.counts.shop or 0
	st.monsters += r.counts.monster or 0
	st.mapflags += r.counts.mapflag or 0
	for _, d in r.decls do
		if d.kind == "script" then
			local name = d.name
			local map, x, y, dir = parsePlacement(d.w1)
			local entry = { name = name, exname = exnameOf(name), kind = "script", path = path, line = d.line, pos = d.pos, endpos = d.endpos,
				map = map, x = x, y = y, dir = dir, file = file }
			entry.events = scanEvents(src, d.pos + 1, d.endpos - 1)
			self:register(entry)
			st.scripts += 1
			added += 1
		elseif d.kind == "function" then
			local lname = lower(d.name)
			local entry = { name = d.name, exname = d.name, kind = "function", path = path, line = d.line, pos = d.pos, endpos = d.endpos, file = file }
			if self.funcs[lname] then
				self.diagnostics[#self.diagnostics + 1] = { file = path, line = d.line, message = "duplicate function " .. d.name }
			end
			self.funcs[lname] = entry
			self.uf[lname] = true
			st.functions += 1
		elseif d.kind == "duplicate" then
			local map, x, y, dir = parsePlacement(d.w1)
			local entry = { name = d.name, exname = exnameOf(d.name), kind = "duplicate", template = d.template, path = path, line = d.line,
				map = map, x = x, y = y, dir = dir, file = file }
			local root = self:rootOf(entry)
			entry.events = root and root.events or nil
			self:register(entry)
			st.duplicates += 1
			added += 1
		end
	end
	return added, r.errors
end

function Loader:register(entry)
	if self.npcs[entry.exname] then
		self.stats.errors += 1
		self.diagnostics[#self.diagnostics + 1] = { file = entry.path, line = entry.line, message = "duplicate NPC name " .. entry.exname }
		return
	end
	self.npcs[entry.exname] = entry
	self.npcsLower[lower(entry.exname)] = entry
	self.npcList[#self.npcList + 1] = entry
	if entry.events then
		for _, label in entry.events do
			local l = lower(label)
			local arr = self.byLabel[l]
			if not arr then arr = {}; self.byLabel[l] = arr end
			arr[#arr + 1] = entry
		end
	end
end

-- the entry that owns the program and the `.` variables: follows duplicate chains to the script (nil when the template is not a script)
function Loader:rootOf(entry)
	local seen = 0
	while entry and entry.kind == "duplicate" do
		entry = self.npcs[entry.template]
		seen += 1
		if seen > 64 then return nil end
	end
	if entry and entry.kind == "script" then return entry end
	return nil
end

function Loader:text(file)
	if file.text then return file.text end
	return self.readText(file.path)
end

-- Parses and lowers the body of `entry` (a script, function or duplicate). Returns prog or nil, err. The result is cached on the entry; a
-- duplicate gets the template's program table itself (one body for all clones).
function Loader:program(entry)
	local p = entry.prog
	if p then return p end
	if entry.loadError then return nil, entry.loadError end
	if entry.kind == "duplicate" then
		local root = self:rootOf(entry)
		if not root then
			entry.loadError = { cause = "duplicate of a non-script or unknown template '" .. tostring(entry.template) .. "'" }
			return nil, entry.loadError
		end
		local rp, err = self:program(root)
		if not rp then entry.loadError = err; return nil, err end
		entry.prog = rp
		return rp
	end
	local text = self:text(entry.file)
	local body = sub(text, entry.pos + 1, entry.endpos - 1)
	local ast, info = Parser.parse(body, self.builtins, self.uf)
	if not ast then
		self.stats.parseFailed += 1
		entry.loadError = { cause = "parse:" .. tostring(info.cause), detail = info.detail, line = info.line }
		return nil, entry.loadError
	end
	local prog, lerr = Lower.lower(ast, { name = entry.name, file = entry.path, builtins = self.builtins, userfuncs = self.uf })
	if not prog then
		self.stats.parseFailed += 1
		entry.loadError = { cause = "lower:" .. tostring(lerr.cause), line = lerr.line }
		return nil, entry.loadError
	end
	prog.baseLine = entry.line
	entry.prog = prog
	self.stats.parsed += 1
	return prog
end

-- the `.` variable table of an NPC (shared by the template and every duplicate clone)
function Loader:varsOf(entry)
	local root = entry.kind == "duplicate" and self:rootOf(entry) or entry
	root = root or entry
	local v = root.vars
	if not v then v = {}; root.vars = v end
	entry.vars = v
	return v
end

function Loader:find(name: string)
	return self.npcs[name]
end

-- registries handed to the interpreter environment (world.luau env.funcs / env.npcvars)
function Loader:funcRegistry()
	local L = self
	return setmetatable({}, { __index = function(t, lname)
		local e = L.funcs[lname]
		if not e then return nil end
		local p = L:program(e)
		if p then rawset(t, lname, p) end
		return p
	end })
end
function Loader:npcVarsRegistry()
	local L = self
	return setmetatable({}, { __index = function(_, name)
		local e = L.npcs[name]
		if e then return L:varsOf(e) end
		return nil
	end })
end

-- Drops the cached program of an entry (memory pressure / reload); the next use parses again.
function Loader:evict(entry)
	if entry.kind == "script" or entry.kind == "function" then entry.prog = nil end
	entry.loadError = nil
end

return Loader
