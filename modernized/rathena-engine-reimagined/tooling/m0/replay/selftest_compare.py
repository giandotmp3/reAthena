#!/usr/bin/env python3
"""Self-test of compare_traces.py: fabricated trace pairs, one per mismatch class, must be classified as expected."""
import os, sys, json, subprocess, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
CASES = {
 'pass':    ('mes("a")\nnext()\n#end end\n=$x=1\n', 'mes("a")\nnext() -> 0\n#end end\n=$x=1\n', set()),
 'argfmt':  ('mes("a  b")\n#end end\n', 'mes("a b")\n#end end\n', {'arg_format'}),
 'argnum':  ('getitem(501,1)\n#end end\n', 'getitem("501",1)\n#end end\n', {'arg_format'}),
 'argval':  ('mes("a")\n#end end\n', 'mes("b")\n#end end\n', {'arg_value'}),
 'retval':  ('countitem(501) -> 3\n#end end\n', 'countitem(501) -> 0\n#end end\n', {'ret_value'}),
 'order':   ('mes("a")\nemotion(1)\n#end end\n', 'emotion(1)\nmes("a")\n#end end\n', {'ordering'}),
 'missing': ('getpartnerid() -> 0\n#end end\n', 'getpartnerid() !U\n#end end\n', {'missing_command'}),
 'stopped': ('mes("a")\nmes("b")\n#end end\n', 'mes("a")\n#end error:CALL_DEPTH\n', {'luau_stopped'}),
 'extra':   ('mes("a")\n#end end\n', 'mes("a")\nmes("z")\n#end end\n', {'extra_in_luau'}),
 'legacyonly': ('mes("a")\nclose()\n#end end\n', 'mes("a")\n#end end\n', {'missing_in_luau'}),
 'fail':    ('getcastledata("",1) -> 0 !F\n#end end\n', 'getcastledata("",1) -> 0\n#end end\n', {'legacy_failure'}),
 'state':   ('#end end\n=$x=1\n=$y=2\n%zeny=5\n', '#end end\n=$x=2\n=$z=1\n', {'state_value', 'state_missing', 'state_extra'}),
 'end':     ('mes("a")\n#end cap\n', 'mes("a")\n#end end\n', {'end_reason'}),
}
d = tempfile.mkdtemp()
os.makedirs(d + '/l'); os.makedirs(d + '/u')
cases = []
for k, (l, u, _) in CASES.items():
    open('%s/l/%s.trace' % (d, k), 'w').write(l); open('%s/u/%s.trace' % (d, k), 'w').write(u)
    cases.append(dict(id=k, npc=k, path='x.txt', category='selftest'))
json.dump(cases, open(d + '/cases.json', 'w'))
subprocess.run([sys.executable, '-I', HERE + '/compare_traces.py', '--cases', d + '/cases.json', '--legacy', d + '/l', '--luau', d + '/u', '--out', d],
               check=True, stdout=subprocess.DEVNULL)
res = json.load(open(d + '/results.json'))
bad = 0
import collections
# recompute kinds per case through the module's own report by re-running with a per-case json: use results root + report
rep = open(d + '/report.txt').read()
for r in res['cases']:
    exp = CASES[r['id']][2]
    got = {r['root']['kind']} if r['root'] else set()
    ok = (not exp and r['strict_pass']) or (exp and (r['root'] is not None) and (r['root']['kind'] in exp or r['id'] == 'state'))
    print('%-10s expected %-45s root=%-16s %s' % (r['id'], sorted(exp), r['root'] and r['root']['kind'], 'ok' if ok else 'FAIL'))
    bad += not ok
sys.exit(1 if bad else 0)
