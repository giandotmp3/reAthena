#!/bin/sh
# regenerates interp_nocount.luau (interp.luau minus the per-op step counter) for the overhead comparison
cd "$(dirname "$0")"
python3 - <<'PY'
s=open('interp.luau').read()
a='''			local ops = st.ops + 1
			st.ops = ops
			if ops > st.limit then terr(st, "BUDGET_EXCEEDED", { what = "commands" }) end
'''
assert a in s
open('interp_nocount.luau','w').write(s.replace(a,''))
PY
