#!/usr/bin/env python3
"""Pick the WIDENED conformance corpus: >= 150 additional NPC scripts from BOTH load sets (renewal: npc/re/scripts_main.conf,
pre-renewal: npc/pre-re/scripts_main.conf), disjoint from the first corpus (corpus/corpus.json), deterministic. Writes
corpus/corpus_wide.json (each entry has "set": "re"|"pre").

  python3 -I select_corpus_wide.py [--repo /home/user/reAthena] [--per-set 80]

Stratification per load set (quota, then the remaining quota is filled from the generic pool):
  dense-flow    the densest goto/callsub/label NPCs (<= 2 per file, files ranked by total density)
  array-string  the NPCs with the most string/array command uses
  switch-menu   switch + select/menu NPCs, sampled evenly over the sorted list
  simple        short dialogue NPCs (sampled evenly)
  spread        one NPC per so-far-unused directory (breadth over the directory tree)
Excluded (the replay drives ONE player, no clock, no event queue): bodies that use timers/clock events/NPC-to-NPC events/battlegrounds/
instances/waiting rooms/rid attach/party+guild member iteration/sleep, shops and floating On* event-only NPCs without an interactive start."""
import os, re, sys, json, argparse, collections

ap = argparse.ArgumentParser()
ap.add_argument('--repo', default=os.environ.get('RATHENA_REPO', '/home/user/reAthena'))
ap.add_argument('--per-set', type=int, default=80)
ap.add_argument('--out', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'corpus', 'corpus_wide.json'))
args = ap.parse_args()
REPO = args.repo
HERE = os.path.dirname(os.path.abspath(__file__))
first = json.load(open(os.path.join(HERE, 'corpus', 'corpus.json')))['chosen']
first_names = {(n['name']) for n in first}

def walk(conf, out):
    with open(os.path.join(REPO, conf), 'rb') as f:
        for raw in f.read().decode('latin-1').splitlines():
            line = raw.split('//', 1)[0].strip()
            m = re.match(r'^(npc|import)\s*:\s*(\S.*)$', line)
            if not m: continue
            if m.group(1) == 'npc': out.append(m.group(2).strip())
            else: walk(m.group(2).strip(), out)

HEAD = re.compile(r'^(?:([\w@.\-]+),(\d+),(\d+),(\d+)|-)\tscript\t([^\t]+)\t([^\t,]+),\{', re.M)

def body_end(text, i):
    depth, n, in_s = 1, len(text), False
    while i < n:
        c = text[i]
        if in_s:
            if c == '\\': i += 1
            elif c == '"': in_s = False
        else:
            if c == '"': in_s = True
            elif c == '/' and text[i+1:i+2] == '/':
                j = text.find('\n', i); i = n if j < 0 else j
                continue
            elif c == '{': depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0: return i + 1
        i += 1
    return None

STRFN = re.compile(r'\b(setarray|explode|implode|substr|charat|strlen|replacestr|countstr|strpos|getarraysize|copyarray|deletearray|sprintf|upperstring|lowerstring|cleararray|array_\w+|getd|setd)\b')
EXCL = re.compile(r'\b(initnpctimer|startnpctimer|stopnpctimer|setnpctimer|addtimer|OnTimer\d*|OnClock\d*|OnHour\d*|OnMinute\d*|OnDay\d*|OnSun\w*|OnMon\w*|OnTue\w*|OnWed\w*|OnThu\w*|OnFri\w*|OnSat\w*|'
                  r'donpcevent|doevent|bg_\w+|waitingroom\w*|delwaitingroom|instance_\w+|attachrid|addrid|detachrid|getpartymember|getguildmember|getmapunits|'
                  r'sleep2?|areaannounce|OnTouch\w*|OnPC\w+|OnNPCKill\w*|OnMobKill\w*|OnWhisperGlobal|OnAgit\w*|agit\w+|OnEmperium\w*|OnDisable|OnEnable|start_war|end_war)\b', re.I)
OTHER = re.compile(r'\b(monster|areamonster|warp|getguild\w*|\w*castle\w*)\b')
INTERACT = re.compile(r'\b(mes|select|menu|input|next|close2?|prompt)\b')

def scan(setname, conf):
    files = []
    walk(conf, files)
    files = [f for f in dict.fromkeys(files) if f.endswith('.txt')]
    npcs = []
    for f in files:
        p = os.path.join(REPO, f)
        if not os.path.exists(p): continue
        text = open(p, 'rb').read().decode('latin-1').replace('\r\n', '\n')
        text = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)  # NPCs inside block comments do not exist
        for m in HEAD.finditer(text):
            name = m.group(5)
            if '::' in name: continue
            end = body_end(text, m.end())
            if end is None: continue
            body = text[m.end():end]
            n_lines = body.count('\n') + 1
            npcs.append(dict(set=setname, file=f, name=name, line=text.count('\n', 0, m.start()) + 1, lines=n_lines, map=m.group(1) or '-',
                callsub=len(re.findall(r'\bcallsub\b', body)), goto=len(re.findall(r'\bgoto\b', body)),
                switch=len(re.findall(r'\bswitch\s*\(', body)), select=len(re.findall(r'\b(select|prompt)\s*\(', body)) + len(re.findall(r'\bmenu\b', body)),
                strfn=len(STRFN.findall(body)), mes=len(re.findall(r'\bmes\b', body)), callfunc=len(re.findall(r'\bcallfunc\b', body)),
                labels=len(re.findall(r'^\s*[A-Za-z_]\w*:\s*$', body, re.M)),
                excl=len(EXCL.findall(body)), other=len(OTHER.findall(body)), inter=len(INTERACT.findall(body)),
                onlabels=len(re.findall(r'^\s*On\w+:', body, re.M))))
    cnt = collections.Counter(n['name'] for n in npcs)
    return [n for n in npcs if cnt[n['name']] == 1], files

chosen_all = []
summary = {}
for setname, conf in (('re', 'npc/re/scripts_main.conf'), ('pre', 'npc/pre-re/scripts_main.conf')):
    npcs, files = scan(setname, conf)
    ok = [n for n in npcs if n['excl'] == 0 and n['other'] <= 5 and n['inter'] >= 2 and n['mes'] >= 1 and n['lines'] <= 700
          and n['name'] not in first_names and n['onlabels'] <= 2]
    dens = lambda n: n['callsub'] * 3 + n['goto'] * 3 + n['labels'] * 2 + n['callfunc']
    chosen, perfile, perdir = [], collections.Counter(), collections.Counter()
    def take(cat, cands, k, per_file=2):
        got = 0
        for n in cands:
            if got >= k: break
            if n['name'] in {c['name'] for c in chosen}: continue
            if perfile[n['file']] >= per_file: continue
            chosen.append(dict(n, category=cat)); perfile[n['file']] += 1; perdir[os.path.dirname(n['file'])] += 1; got += 1
        return got
    K = args.per_set
    q = dict(dense=int(K * .30), strs=int(K * .25), sw=int(K * .15), simple=int(K * .15))
    # densest files first, the densest NPC of each, then the next
    bydens = collections.defaultdict(int)
    for n in ok: bydens[n['file']] += dens(n)
    flow = sorted([n for n in ok if dens(n) >= 4], key=lambda n: (-bydens[n['file']], -dens(n), n['file'], n['line']))
    take('dense-flow', flow, q['dense'])
    strs = sorted([n for n in ok if n['strfn'] >= 3], key=lambda n: (-n['strfn'], n['file'], n['line']))
    take('array-string', strs, q['strs'])
    sw = sorted([n for n in ok if n['switch'] >= 1 and n['select'] >= 1], key=lambda n: (-(n['switch'] * 2 + n['select']), n['file'], n['line']))
    take('switch-menu', sw[::max(1, len(sw) // (q['sw'] * 3))], q['sw'])
    simple = sorted([n for n in ok if n['lines'] <= 60 and n['callsub'] == 0 and n['goto'] == 0 and n['mes'] >= 2], key=lambda n: (n['file'], n['line']))
    take('simple-dialogue', simple[::max(1, len(simple) // (q['simple'] * 3))], q['simple'])
    # breadth: directories not yet used, then anything
    rest = sorted(ok, key=lambda n: (perdir[os.path.dirname(n['file'])], -dens(n) - n['switch'], n['file'], n['line']))
    # rotate by directory: always take from the least-used directory next
    while len(chosen) < K:
        rest = [n for n in rest if n['name'] not in {c['name'] for c in chosen} and perfile[n['file']] < 2]
        if not rest: break
        rest.sort(key=lambda n: (perdir[os.path.dirname(n['file'])], n['file'], n['line']))
        take('spread', rest[:1], 1)
    chosen_all += chosen
    c = collections.Counter(x['category'] for x in chosen)
    summary[setname] = dict(files_in_set=len(files), npcs=len(npcs), eligible=len(ok), chosen=len(chosen),
                            dirs=len(perdir), files=len(perfile), by_category=dict(c))

json.dump(dict(summary=summary, chosen=chosen_all), open(args.out, 'w'), indent=1)
print(json.dumps(summary, indent=1))
print(len(chosen_all), 'NPCs')
