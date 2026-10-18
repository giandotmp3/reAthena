#!/usr/bin/env python3
"""Local runner: executes tests/main.luau with Lua via lupa.

The sources are written in the Lua/Luau common subset. Inside the engine the mod
loader supplies `import`; this runner supplies an equivalent. Requires `pip install lupa`.
"""
import os
import sys

from lupa import LuaRuntime

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BOOT = r"""
local root = ...
local cache = {}
function import(name)
  if cache[name] == nil then
    local f = assert(io.open(root .. "/" .. name .. ".luau", "r"))
    local src = f:read("a"); f:close()
    local chunk = assert(load(src, "@" .. name, "t", _G))
    cache[name] = chunk()
  end
  return cache[name]
end
return import("tests/main")
"""


def main() -> int:
    lua = LuaRuntime(unpack_returned_tuples=True)
    code = lua.execute(BOOT, ROOT)
    return int(code or 0)


if __name__ == "__main__":
    sys.exit(main())
