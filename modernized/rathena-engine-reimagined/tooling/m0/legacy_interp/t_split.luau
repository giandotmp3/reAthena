local ds = require("./declsplit")
local man = require("./manifest")
local t0=os.clock()
local tot = {}
local errs = 0
for _, set in {"re","pre"} do
 for _, fi in man.sets[set] do
  local f = man.files[fi]
  local text = require("./data/"..f.mod)[1]
  local r = ds.split(text)
  for k,v in r.counts do tot[k.."@"..set]=(tot[k.."@"..set] or 0)+v end
  for _,e in r.errors do errs+=1; print(f.path, ds.lineof(text,e[2]), e[1]) end
 end
end
print(os.clock()-t0)
local ks={} for k in tot do table.insert(ks,k) end table.sort(ks) for _,k in ks do print(k,tot[k]) end
