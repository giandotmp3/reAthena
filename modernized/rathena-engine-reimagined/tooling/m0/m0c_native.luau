-- Which native library calls can run past the budget? Each gets a fresh 0.2 s budget; report how long it really ran.
local function t(name, fn)
  set_budget(0.2); local t0 = clock(); local ok, err = pcall(fn)
  print(string.format("%-44s %-9s %.3f s %s", name, ok and "completed" or "stopped", clock() - t0, ok and "" or tostring(err):sub(-26)))
end
local big = {} for i = 1, 4e6 do big[i] = (i * 7919) % 1000003 end
set_budget(0)
t("table.sort 4e6 numbers, default comparator", function() table.sort(big) end)
t("table.concat 4e6 numbers", function() local s = {} for i = 1, 4e6 do s[i] = "x" end return #table.concat(s) end)
t("string.rep 5e7 (64MB cap)", function() return #string.rep("ab", 3e7) end)
t("string.gsub over 5e6 chars", function() return #string.gsub(string.rep("a", 5e6), "a", "bb") end)
t("string.find pathological backtracking", function() return string.find(string.rep("a", 28), string.rep("a?", 28) .. string.rep("a", 28)) end)
t("table.unpack 1e5", function() return select("#", table.unpack(big, 1, 1e5)) end)
