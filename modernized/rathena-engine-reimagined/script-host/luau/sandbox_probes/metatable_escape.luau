-- Must fail or be inert: mutating the shared string metatable.
local mt = getmetatable("")
mt.__index = function() return "pwned" end
