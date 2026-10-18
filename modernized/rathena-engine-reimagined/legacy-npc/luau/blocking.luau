--!nonstrict
-- Blocking commands as yields (architecture section 7): next close close2 menu select prompt input sleep sleep2 progressbar progressbar_npc.
-- The interpreter calls st.block(st, kind, payload) from every blocking command (interp.luau `yield`); this module builds those
-- adapters. `mes` text is buffered in st.mesbuf (commands_rt.luau) and leaves with the next dialogue request, because script-host's
-- dialogue has no separate "message" request:
--
--   next / close / close2   -> say(buffered text); returns when the client acknowledges
--   menu / select / prompt  -> say(buffered text) if there is any, then choose(options) -> 1-based index of the VISIBLE option
--   input                   -> input(buffered text) -> number (clamped by the command) or string
--   sleep / sleep2 / progressbar / progressbar_npc -> wait(ms)
--
-- A disconnect cancels the coroutine inside script-host (DialogueManager::on_disconnect): none of the interpreter's code runs again,
-- and every persistent register was written when the blocking command started (interp.luau `yield` flushes), so nothing is lost.
local M = {}

local function flushText(st): string
	local buf = st.mesbuf
	if not buf or #buf == 0 then return "" end
	local text = table.concat(buf, "\n")
	table.clear(buf)
	return text
end
M.flushText = flushText

-- api: { say = fn(text), choose = fn(options) -> index, input = fn(prompt) -> text, wait = fn(ms) } (script-host: the `dialogue` global
-- or the bare say/choose/input/wait globals)
function M.dialogue(api)
	return function(st, kind, payload)
		if kind == "next" or kind == "close" or kind == "close2" then
			api.say(flushText(st))
			return nil
		elseif kind == "menu" or kind == "select" or kind == "prompt" then
			local text = flushText(st)
			if text ~= "" then api.say(text) end
			return api.choose(payload)
		elseif kind == "input" then
			local v = api.input(flushText(st))
			if payload and not payload.str then v = tonumber(v) or 0 end
			return v
		elseif kind == "sleep" or kind == "sleep2" or kind == "progressbar" or kind == "progressbar_npc" then
			api.wait(payload or 0)
			return nil
		end
		error({ type = "BAD_BLOCK_KIND", kind = kind }, 0)
	end
end

-- A dialogue API that answers from a script and records every request: for tests that run without script-host.
-- answers: array consumed by choose/input (a number picks, a string types); say/wait need no answer.
function M.scripted(answers)
	local api: any = { log = {}, answers = answers or {}, pos = 0 }
	local function nextAnswer(default)
		api.pos += 1
		local a = api.answers[api.pos]
		if a == nil then return default end
		return a
	end
	-- api.onRequest(kind, payload): optional callback run while the "client" is being asked (tests inject events here)
	local function seen(kind, payload)
		if api.onRequest then api.onRequest(kind, payload) end
	end
	function api.say(text) api.log[#api.log + 1] = { "say", text }; seen("say", text) end
	function api.choose(options)
		api.log[#api.log + 1] = { "choose", table.concat(options, "|") }
		seen("choose", options)
		local a = nextAnswer(1)
		if a == "cancel" then error({ type = "CANCELLED" }, 0) end
		return math.max(1, math.min(#options, a))
	end
	function api.input(prompt) api.log[#api.log + 1] = { "input", prompt }; seen("input", prompt); return nextAnswer("") end
	function api.wait(ms) api.log[#api.log + 1] = { "wait", ms }; seen("wait", ms) end
	return api
end

return M
