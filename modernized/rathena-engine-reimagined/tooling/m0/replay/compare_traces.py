#!/usr/bin/env python3
"""Diff legacy-VM traces against legacy_interp traces and classify the mismatches.

Trace format (see TRACE_FORMAT.md):   name(arg,...) [-> ret] [!F] [!U]   one per host call, then
  #end <reason>, then final-state lines  =var=value | %param=n | +item id=n

Mismatch classes (per aligned record):
  arg_format       same command, arguments equal after normalisation (number vs numeric string, whitespace, case)
  arg_value        same command, different argument values (or argument count)
  ret_format       return values equal only after normalisation
  ret_value        same command, different return value (absent return == 0)
  ordering         same multiset of commands in a window, different order
  missing_command  legacy_interp has no implementation (recorded as soft stub '!U'), legacy executed it
  missing_in_luau  legacy executed a command that legacy_interp never reached/emitted (not an unsupported stub)
  extra_in_luau    legacy_interp emitted a command the legacy VM did not
  luau_stopped     legacy_interp ended early with an error (tail of legacy trace lost)
  legacy_failure   legacy buildin returned SCRIPT_CMD_FAILURE (`!F`) and legacy_interp did not report the same failure
  luau_failure     legacy_interp reported a failure (`!F`) the legacy buildin did not
  end_reason       the two runs ended for different reasons
  state_value / state_missing / state_extra   final variable / param / item state
"""
import os, re, sys, json, argparse, difflib, collections

ap = argparse.ArgumentParser()
ap.add_argument('--cases', required=True)
ap.add_argument('--legacy', required=True)
ap.add_argument('--luau', required=True)
ap.add_argument('--out', required=True)
ap.add_argument('--examples', type=int, default=5)
ap.add_argument('--tag', default='')
args = ap.parse_args()

# commands whose result depends on world data the Luau harness does not model (items, mobs, DB constants, clock, player)
WORLD = set('''getitemname getiteminfo getitemslots countitem countitem2 checkweight checkweight2 getmonsterinfo mobcount gettime gettimetick
gettimestr getmapusers getmapxy strcharinfo strnpcinfo checkquest isbegin_quest questprogress getskilllv getequipid getequipname isequipped
getnpcid getcharid getgmlevel is_party_leader checkre getpartyname getguildname getguildmaster getcastlename getcastledata getbattleflag
getunitdata getrefine getequiprefinerycnt getinventorylist getd rand readparam eaclass roclass getgroupitem getpetinfo gethominfo getmercinfo
checkcell getareausers getusers checkvending checkchatting agitcheck agitcheck2 agitcheck3 isloggedin getmapflag getpartymember
gettimestr getnameditem mobuid getcharip getlook getbrokenid getequipisequiped getequipweaponlv getequipisenableref getequipisidentify
getequippercentrefinery getequiprefinerycnt getenchantgrade getcharid getrefine isbegin_quest checkdragon checkcart checkfalcon checkriding
checkwug checkmadogear checkoption getmercinfo getpetinfo getelementalinfo gethominfo'''.split())

CALL = re.compile(r'^([a-z_0-9@]+)\((.*?)\)((?: -> .*?)?)((?: !F)?)((?: !U)?)$')

def tokens(s):
    out, i, n = [], 0, len(s)
    while i < n:
        if s[i] == '"':
            j = i + 1
            while j < n and s[j] != '"':
                j += 2 if s[j] == '\\' else 1
            out.append(s[i:j + 1]); i = j + 1
        else:
            j = s.find(',', i)
            if j < 0: j = n
            out.append(s[i:j]); i = j
        if i < n and s[i] == ',': i += 1
    return out

def parse(path):
    r = dict(calls=[], end=None, state={}, extra=[], raw=[])
    try:
        data = open(path, 'rb').read().decode('latin-1')
    except FileNotFoundError:
        r['end'] = 'missing-trace'; return r
    phase = 0
    for ln, line in enumerate(data.split('\n'), 1):
        if not line: continue
        if line.startswith('#end '):
            r['end'] = line[5:]; phase = 1; continue
        if line.startswith('#'):
            r['extra'].append(line); continue
        if phase == 0:
            m = CALL.match(line)
            if not m:
                r['extra'].append('UNPARSED:' + line); continue
            name, a, ret, f, u = m.groups()
            r['calls'].append(dict(name=name, args=tokens(a), ret=ret[4:] if ret else None, F=bool(f), U=bool(u), raw=line, n=ln))
        else:
            if line.startswith('='):
                k, _, v = line[1:].rpartition('=') if False else (None, None, None)
                m = re.match(r'^=(.*?)=(".*"|-?[\d.e+]+|<unset>)$', line)
                if m: r['state'][m.group(1)] = m.group(2)
                else: r['extra'].append('UNPARSED:' + line)
            elif line.startswith('%') or line.startswith('+item '):
                k, _, v = line.partition('=')
                r['state'][k] = v
            else:
                r['extra'].append('UNPARSED:' + line)
    return r

def unq(t):
    if len(t) >= 2 and t[0] == '"' and t[-1] == '"':
        s = t[1:-1]
        return re.sub(r'\\(x[0-9a-f]{2}|.)', lambda m: {'n': '\n', 'r': '\r', 't': '\t'}.get(m.group(1), chr(int(m.group(1)[1:], 16)) if m.group(1)[0] == 'x' and len(m.group(1)) == 3 else m.group(1)), s)
    return None

def num(t):
    try: return float(t)
    except Exception: return None

def norm(t):
    s = unq(t)
    if s is not None:
        n = num(s.strip())
        return ('n', n) if n is not None else ('s', re.sub(r'\s+', ' ', s).strip().lower())
    n = num(t)
    return ('n', n) if n is not None else ('o', t)

def same_raw(a, b): return a == b
def same_fmt(a, b): return norm(a) == norm(b)

def ret_of(c): return c['ret'] if c['ret'] is not None else '0'

def classify_pair(L, U):
    """list of (kind, detail) for one aligned pair with equal command names"""
    out = []
    if U['U']:
        out.append(('missing_command', 'no implementation in legacy_interp (soft stub)'))
        return out
    if L['F'] and not U['F']:
        out.append(('legacy_failure', 'legacy buildin returned failure, legacy_interp did not'))
    elif U['F'] and not L['F']:
        out.append(('luau_failure', 'legacy_interp reported failure, legacy buildin did not'))
    la, ua = L['args'], U['args']
    if la != ua:
        if len(la) != len(ua):
            out.append(('arg_value', 'argument count %d vs %d' % (len(la), len(ua))))
        else:
            kinds = set()
            for x, y in zip(la, ua):
                if x != y: kinds.add('arg_format' if same_fmt(x, y) else 'arg_value')
            out.append(('arg_value' if 'arg_value' in kinds else 'arg_format', 'arguments differ'))
    lr, ur = ret_of(L), ret_of(U)
    if lr != ur:
        out.append(('ret_format' if same_fmt(lr, ur) else 'ret_value', 'return differs'))
    return out

def compare_case(c):
    L = parse(os.path.join(args.legacy, c['id'] + '.trace'))
    U = parse(os.path.join(args.luau, c['id'] + '.trace'))
    mm = []  # mismatches
    def add(kind, detail, l=None, u=None, cmd=None):
        mm.append(dict(kind=kind, detail=detail, legacy=l['raw'] if l else None, luau=u['raw'] if u else None,
                       legacy_line=l['n'] if l else None, luau_line=u['n'] if u else None, cmd=cmd or (l or u)['name'],
                       lrec=l, urec=u))
    lc, uc = L['calls'], U['calls']
    sm = difflib.SequenceMatcher(None, [x['name'] for x in lc], [x['name'] for x in uc], autojunk=False)
    luau_err = U['end'] is not None and U['end'].startswith('error:')
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            for k in range(i2 - i1):
                for kind, det in classify_pair(lc[i1 + k], uc[j1 + k]):
                    add(kind, det, lc[i1 + k], uc[j1 + k])
            continue
        lb, ub = lc[i1:i2], uc[j1:j2]
        if lb and ub and sorted(x['name'] for x in lb) == sorted(x['name'] for x in ub):
            add('ordering', 'window of %d calls reordered' % len(lb), lb[0], ub[0])
            continue
        # unequal names inside the window: pair what can be paired, the rest is missing / extra
        m = min(len(lb), len(ub))
        for k in range(m):
            add('extra_in_luau' if False else 'missing_in_luau', 'legacy ran %s where legacy_interp ran %s' % (lb[k]['name'], ub[k]['name']), lb[k], ub[k], cmd=lb[k]['name'])
        for x in lb[m:]:
            if luau_err and j2 >= len(uc) and tag != 'insert':
                add('luau_stopped', 'legacy_interp ended with %s; legacy continued' % U['end'], x, None)
                break
            add('missing_in_luau', 'legacy-only call', x, None)
        for y in ub[m:]:
            add('extra_in_luau', 'legacy_interp-only call', None, y)
    # a call that only moved (same name + args, legacy-only here and luau-only nearby) is an ordering difference
    lonly = [m for m in mm if m['kind'] == 'missing_in_luau' and m['urec'] is None]
    uonly = [m for m in mm if m['kind'] == 'extra_in_luau' and m['lrec'] is None]
    for a in lonly:
        for b in uonly:
            if b in mm and a['lrec']['name'] == b['urec']['name'] and a['lrec']['args'] == b['urec']['args'] and abs(a['lrec']['n'] - b['urec']['n']) <= 12:
                mm.remove(b)
                a['kind'] = 'ordering'; a['detail'] = 'call moved: legacy line %d, luau line %d' % (a['lrec']['n'], b['urec']['n'])
                a['luau'] = b['luau']; a['luau_line'] = b['luau_line']
                break
    le, ue = L['end'], U['end']
    if le != ue and not (luau_err and any(x['kind'] == 'luau_stopped' for x in mm)):
        add('end_reason', 'legacy=%s luau=%s' % (le, ue), None, None, cmd='<end>')
    ls, us = L['state'], U['state']
    for k in sorted(set(ls) | set(us)):
        if k in ls and k in us:
            if ls[k] != us[k]:
                add('state_value', '%s legacy=%s luau=%s' % (k, ls[k], us[k]), None, None, cmd=k)
        elif k in ls:
            add('state_missing', '%s=%s only in legacy' % (k, ls[k]), None, None, cmd=k)
        else:
            add('state_extra', '%s=%s only in legacy_interp' % (k, us[k]), None, None, cmd=k)
    return L, U, mm

STATE_KINDS = {'state_value', 'state_missing', 'state_extra', 'end_reason'}
FORMAT_ONLY = {'arg_format', 'ret_format'}

def root_cause(m, cascade_hint):
    """heuristic attribution of a root (first) divergence: interpreter | world | coverage | harness"""
    k, cmd = m['kind'], m['cmd']
    if k == 'missing_command': return 'coverage (command not implemented in legacy_interp)'
    if k == 'legacy_failure': return 'world (legacy buildin failed: needs real world state)'
    if k in ('ret_value', 'ret_format') and cmd in WORLD: return 'world (value comes from game data / clock / player the harness does not model)'
    if k in ('arg_value', 'arg_format') and cmd in WORLD: return 'world'
    return 'unattributed'

def main():
    cases = json.load(open(args.cases))
    res = []
    by_kind = collections.Counter()
    examples = collections.defaultdict(list)
    root_kind = collections.Counter()
    root_attr = collections.Counter()
    root_examples = collections.defaultdict(list)
    for c in cases:
        L, U, mm = compare_case(c)
        call_mm = [m for m in mm if m['kind'] not in STATE_KINDS]
        strict_pass = not mm
        fmt_pass = all(m['kind'] in FORMAT_ONLY for m in mm)
        root = call_mm[0] if call_mm else (mm[0] if mm else None)
        for i, m in enumerate(mm):
            m['cascade'] = (m is not root) and (root is not None)
            by_kind[m['kind']] += 1
            if len(examples[m['kind']]) < 40: examples[m['kind']].append(dict(m, case=c['id'], npc=c['npc'], file=c['path'], line=c.get('line')))
        if root:
            root_kind[root['kind']] += 1
            root['attr'] = root_cause(root, False)
            root_attr[(root['kind'], root['attr'])] += 1
            root_examples[root['kind']].append(dict(root, case=c['id'], npc=c['npc'], file=c['path'], category=c['category']))
        inter = sum(1 for x in L['calls'] if x['name'] in ('select', 'menu', 'prompt', 'input', 'next', 'close2', 'sleep', 'sleep2'))
        res.append(dict(id=c['id'], npc=c['npc'], file=c['path'], line=c.get('line'), interactions=inter, category=c['category'], strict_pass=strict_pass, fmt_pass=fmt_pass,
                        legacy_calls=len(L['calls']), luau_calls=len(U['calls']), legacy_end=L['end'], luau_end=U['end'],
                        mismatches=len(mm), root=root and dict(kind=root['kind'], cmd=root['cmd'], detail=root['detail'], attr=root.get('attr'),
                        legacy=root['legacy'], luau=root['luau'])))
    json.dump(dict(cases=res, by_kind=by_kind, root_kind=root_kind), open(os.path.join(args.out, 'results%s.json' % args.tag), 'w'), indent=1, default=dict)
    # ---- report ----
    out = []
    P = out.append
    n = len(res)
    sp = sum(r['strict_pass'] for r in res); fp = sum(r['fmt_pass'] for r in res)
    P('CASES (NPC x answer-script x player profile): %d    NPCs: %d' % (n, len(set(r['npc'] for r in res))))
    P('strict pass (traces + end state identical): %d / %d = %.1f%%' % (sp, n, 100 * sp / max(1, n)))
    P('pass modulo formatting-only differences:    %d / %d = %.1f%%' % (fp, n, 100 * fp / max(1, n)))
    calls_l = sum(r['legacy_calls'] for r in res)
    P('legacy host calls recorded: %d   legacy_interp: %d' % (calls_l, sum(r['luau_calls'] for r in res)))
    P('cases with >= 1 blocking interaction (next/select/menu/input/...): %d; ended by the interaction cap: %d; median legacy calls per case: %d' % (
        sum(1 for r in res if r['interactions']), sum(1 for r in res if r['legacy_end'] == 'cap'), sorted(r['legacy_calls'] for r in res)[len(res) // 2]))
    P('strict passes that are non-trivial (>= 5 legacy calls and >= 1 interaction): %d' % sum(1 for r in res if r['strict_pass'] and r['legacy_calls'] >= 5 and r['interactions']))
    # per NPC: all 3 variants pass
    per_npc = collections.defaultdict(list)
    for r in res: per_npc[r['npc']].append(r['strict_pass'])
    P('NPCs with all variants passing strictly: %d / %d' % (sum(all(v) for v in per_npc.values()), len(per_npc)))
    P('')
    P('per category (cases strictly passing / total):')
    cat = collections.defaultdict(lambda: [0, 0])
    for r in res:
        cat[r['category']][1] += 1; cat[r['category']][0] += r['strict_pass']
    for k, (a, b) in sorted(cat.items()): P('  %-28s %3d / %3d' % (k, a, b))
    P('')
    P('mismatch classes (all aligned records, cascades included):  count')
    for k, v in by_kind.most_common(): P('  %-18s %6d' % (k, v))
    P('')
    P('ROOT (first) divergence per failing case, by class:')
    for k, v in root_kind.most_common(): P('  %-18s %6d' % (k, v))
    P('ROOT divergence attribution (class, cause):')
    for (k, a), v in sorted(root_attr.items(), key=lambda x: -x[1]): P('  %-16s %-85s %4d' % (k, a, v))
    P('')
    P('LEGACY END REASONS: %s' % dict(collections.Counter(r['legacy_end'] for r in res)))
    P('LUAU   END REASONS: %s' % dict(collections.Counter(r['luau_end'] for r in res)))
    P('')
    P('=== examples per class (all mismatches; case id -> file:line of the NPC) ===')
    for k, v in by_kind.most_common():
        P('')
        P('## %s (%d)' % (k, v))
        seen = set()
        shown = 0
        for e in examples[k]:
            key = (e['cmd'], e['file'])
            if key in seen: continue
            seen.add(key)
            P('  [%s] %s:%s  (NPC "%s"; legacy trace line %s, luau trace line %s)' % (e['case'], e['file'], e['line'], e['npc'], e['legacy_line'], e['luau_line']))
            P('      %s' % e['detail'])
            if e['legacy']: P('      legacy: %s' % e['legacy'][:300])
            if e['luau']: P('      luau  : %s' % e['luau'][:300])
            shown += 1
            if shown >= args.examples: break
    open(os.path.join(args.out, 'report%s.txt' % args.tag), 'w').write('\n'.join(out) + '\n')
    json.dump({k: v[:60] for k, v in root_examples.items()}, open(os.path.join(args.out, 'root_examples%s.json' % args.tag), 'w'), indent=1)
    print('\n'.join(out[:40]))

main()
