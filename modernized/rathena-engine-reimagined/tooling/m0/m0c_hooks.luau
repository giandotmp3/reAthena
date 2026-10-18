-- M0(c): event-bus dispatch cost. Hooks: priority-ordered, cancellable, lazy read-through payload.
local Bus = {}; Bus.__index = Bus
local function newbus() return setmetatable({ hooks = {}, sorted = {} }, Bus) end
function Bus:on(ev, prio, seq, fn)
  local l = self.hooks[ev]; if not l then l = {}; self.hooks[ev] = l end
  l[#l + 1] = { p = prio, s = seq, fn = fn }
  table.sort(l, function(a, b) if a.p ~= b.p then return a.p < b.p end return a.s < b.s end)  -- stable via seq
end
function Bus:emit(ev, payload)
  local l = self.hooks[ev]; if not l then return false end
  for i = 1, #l do
    local r = l[i].fn(payload)
    if r == "cancel" then return true end
  end
  return false
end
local bus = newbus()
local seen = 0
for k = 1, 3 do bus:on("unit:before_move", k * 10, k, function(p) seen = seen + bit32.band(p.x, 1) end) end
for k = 1, 3 do bus:on("unit:moved", k * 10, k, function(p) seen = seen + 1 end) end
local N = tonumber(ARGS[1]) or 1000000
local p = { x = 5, y = 7, id = 42 }
local t0 = clock()
for i = 1, N do bus:emit("unit:before_move", p); bus:emit("unit:moved", p) end
local dt = clock() - t0
local per = dt / (2 * N) * 1e6
print(string.format("dispatch with 3 hooks: %.3f us/event  (%.0f events/s)", per, 2 * N / dt))
local ccu, moves = 1000, 6.7                                  -- walking units fire ~6.7 cell steps/s (legacy unit.cpp)
local evps = ccu * moves * 2
print(string.format("1000 CCU all walking: %.0f events/s = %.2f ms of script per second = %.3f%% of a core, %.3f ms per 20 ms tick",
  evps, evps * per / 1000, evps * per / 1e4, evps * per / 1000 / 50))
print("interrupt=" .. (INTERRUPT_ON == 1 and "ON" or "off"))
