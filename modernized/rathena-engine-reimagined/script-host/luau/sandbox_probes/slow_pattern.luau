-- Pathological pattern; the bounded string.find wrapper must abort it.
return string.find(string.rep("a", 100000), string.rep("a*", 50) .. "b")
