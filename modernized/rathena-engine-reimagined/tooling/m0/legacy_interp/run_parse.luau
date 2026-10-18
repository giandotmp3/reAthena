--!nonstrict
-- Step 2 measurement: parse (and optionally lower) every script body of a load set.
-- usage: luau run_parse.luau re|pre [lower]
local C = require("./corpus")
local parser = require("./parser")
local setname = ({ ... })[1] or "re"
local doLower = ({ ... })[2] == "lower"
local lower = doLower and require("./lower") or nil
local ds, builtins = C.ds, C.builtins

local uf = C.userfuncs()
local files = C.setfiles(setname)

collectgarbage("collect"); collectgarbage("collect")
local mem0 = collectgarbage("count")

local stats = { files = 0, filesWithFail = 0, scripts = 0, functions = 0, dups = 0, decl = {}, failures = 0, ntok = 0, bodyBytes = 0, lines = 0, splitErrors = 0 }
local causes, causeEx = {}, {}
local failFiles = {}
local keep = {}
local tParse, tSplit, tLower = 0, 0, 0
local nodesStmts = 0
for _, fi in files do
	local f = C.man.files[fi]
	if not f.mod then continue end
	stats.files += 1
	local text = C.text(fi)
	local _, nl = string.gsub(text, "\n", "")
	stats.lines += nl
	local t0 = os.clock()
	local r = ds.split(text)
	tSplit += os.clock() - t0
	stats.splitErrors += #r.errors
	for k, v in r.counts do stats.decl[k] = (stats.decl[k] or 0) + v end
	local fileFail = false
	for _, d in r.decls do
		if d.kind == "script" or d.kind == "function" then
			if d.kind == "script" then stats.scripts += 1 else stats.functions += 1 end
			stats.bodyBytes += #d.body
			local t1 = os.clock()
			local ast, info = parser.parse(d.body, builtins, uf)
			tParse += os.clock() - t1
			if ast then
				stats.ntok += info.ntok
				if doLower then
					local t2 = os.clock()
					local prog, lerr = lower.lower(ast, { name = d.name, file = f.path, builtins = builtins, userfuncs = uf })
					tLower += os.clock() - t2
					if prog then keep[#keep + 1] = prog else
						stats.failures += 1; fileFail = true
						local gk = "LOWER: " .. tostring(lerr.cause)
						causes[gk] = (causes[gk] or 0) + 1
						local ex = causeEx[gk]; if not ex then ex = {}; causeEx[gk] = ex end
						if #ex < 4 then ex[#ex + 1] = f.path .. ":" .. (ds.lineof(text, d.pos) + (lerr.line or 1) - 1) .. " [" .. d.name .. "]" end
					end
				else
					keep[#keep + 1] = ast
				end
			else
				stats.failures += 1
				fileFail = true
				local key = info.cause .. (info.detail and (":" .. tostring(info.detail)) or "")
				local gk = info.cause
				if gk == "unknown_command" or gk == "unexpected_token" or gk == "bad_statement" then gk = key end
				causes[gk] = (causes[gk] or 0) + 1
				local ex = causeEx[gk]
				if not ex then ex = {}; causeEx[gk] = ex end
				if #ex < 4 then
					ex[#ex + 1] = f.path .. ":" .. (ds.lineof(text, d.pos) + (info.line or 1) - 1) .. " [" .. d.name .. "]"
				end
			end
		end
	end
	if fileFail then stats.filesWithFail += 1 end
end
collectgarbage("collect"); collectgarbage("collect")
local mem1 = collectgarbage("count")
print(string.format("SET %s lower=%s", setname, tostring(doLower)))
print(string.format("files=%d (with parse failures: %d) split-errors=%d lines=%d bodyBytes=%d", stats.files, stats.filesWithFail, stats.splitErrors, stats.lines, stats.bodyBytes))
local ks = {}
for k in stats.decl do ks[#ks + 1] = k end
table.sort(ks)
for _, k in ks do print("  decl", k, stats.decl[k]) end
print(string.format("script bodies parsed: %d scripts + %d function scripts = %d; failures=%d; ok=%.3f%%", stats.scripts, stats.functions, stats.scripts + stats.functions, stats.failures, 100 * (1 - stats.failures / (stats.scripts + stats.functions))))
if doLower then print(string.format("lowering: hoisted callsub/callfunc out of expressions=%d, left as nested (under ?:, &&, ||)=%d, ops=%d", lower.stats.hoisted, lower.stats.nested, lower.stats.ops)) end
print(string.format("tokens=%d  split_time=%.3fs  parse_time=%.3fs  lower_time=%.3fs", stats.ntok, tSplit, tParse, tLower))
print(string.format("retained %s memory delta = %.1f MB (%.1f bytes/token)", doLower and "LOWERED programs" or "AST", (mem1 - mem0) / 1024, (mem1 - mem0) * 1024 / math.max(stats.ntok, 1)))
local cs = {}
for k, v in causes do cs[#cs + 1] = { k, v } end
table.sort(cs, function(a, b) return a[2] > b[2] end)
print("FAILURE CAUSES:")
for _, c in cs do print(string.format("  %5d  %s    e.g. %s", c[2], c[1], table.concat(causeEx[c[1]], " | "))) end
local q = {}
for k, v in parser.quirks do q[#q + 1] = k .. "=" .. v end
print("rA-quirk statement terminators accepted (',' ')' ':' in place of ';'): " .. table.concat(q, " "))
local le, ln = {}, 0
for k, v in parser.lenient do le[#le + 1] = k .. "=" .. v; ln += v end
table.sort(le)
print("lenient paren-less zero-arg calls in expression position (real parser would reject): total=" .. ln .. " " .. table.concat(le, " "))
