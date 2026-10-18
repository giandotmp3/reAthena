-- M0(c): sandbox probes. Each runs under pcall; PASS means the hostile action was contained.
-- probe: an erroring action is contained. check: a passing assertion means the dangerous API is absent.
local function probe(name, fn)
  local ok, err = pcall(fn)
  print(string.format("%-34s %s  %s", name, ok and "ESCAPED/ALLOWED" or "contained", ok and "" or tostring(err):sub(1, 60)))
end
local function check(name, fn)
  local ok, err = pcall(fn)
  print(string.format("%-34s %s  %s", name, ok and "absent (contained)" or "PRESENT", ok and "" or tostring(err):sub(1, 60)))
end
probe("write to shared global (string)", function() string.evil = 1 end)
probe("getmetatable('').__index write", function() getmetatable("").__index.evil = 1 end)
probe("setmetatable on string meta", function() setmetatable(getmetatable(""), {}) end)
probe("rawset into math", function() rawset(math, "evil", 1) end)
check("os.execute present", function() assert(os == nil or os.execute == nil, "os.execute available") end)
check("io present", function() assert(io == nil, "io available") end)
check("debug.getinfo present", function() assert(debug == nil or debug.getinfo == nil, "debug.getinfo available") end)
check("loadstring present", function() assert(loadstring == nil, "loadstring is available") end)
check("getfenv present", function() assert(getfenv == nil, "getfenv is available") end)
probe("memory bomb string.rep", function() return #string.rep("x", 1e9) end)
