--!nonstrict
-- shared helpers: load corpus files by manifest index (replay harness glue; copy of tooling/m0/legacy_interp/corpus.luau; the manifest and the
-- data/ modules are generated into the harness scratch directory by tooling/m0/legacy_interp/prep.py, they are not part of the unit)
local man = require("./manifest")
local builtins = require("./builtins")
local ds = require("./declsplit")
local C = {}
C.man, C.builtins, C.ds = man, builtins, ds
function C.text(fi: number): string
	return require("./data/" .. man.files[fi].mod)[1]
end
function C.setfiles(set: string): { number }
	return man.sets[set]
end
-- names of all `function script` declarations over the union of both load sets
function C.userfuncs(): { [string]: boolean }
	local uf = {}
	for fi = 1, #man.files do
		local f = man.files[fi]
		if f.mod then
			local text = C.text(fi)
			if string.find(text, "function\tscript", 1, true) then
				local r = ds.split(text)
				for _, d in r.decls do
					if d.kind == "function" then uf[string.lower(d.name)] = true end
				end
			end
		end
	end
	return uf
end
return C
