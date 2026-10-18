#!/usr/bin/env python3
"""Regenerate the replay_hook.inc section of legacy-replay-hook.patch from an edited copy (maintenance helper):
   python3 -I regen_patch.py <build>/rA/src/map/replay_hook.inc
Only the last section of the patch (the new file) is rewritten; the script.cpp/map.cpp/clif.cpp hunks are untouched."""
import sys, os
here = os.path.dirname(os.path.abspath(__file__))
p = os.path.join(here, 'legacy-replay-hook.patch')
txt = open(p, 'rb').read().decode('latin-1')
i = txt.index('--- /dev/null\n+++ b/src/map/replay_hook.inc\n')
src = open(sys.argv[1], 'rb').read().decode('latin-1').split('\n')
if src and src[-1] == '': src.pop()
new = '--- /dev/null\n+++ b/src/map/replay_hook.inc\n@@ -0,0 +1,%d @@\n' % len(src) + ''.join('+' + l + '\n' for l in src)
open(p, 'wb').write((txt[:i] + new).encode('latin-1'))
print('patch rewritten:', len(src), 'lines')
