#!/usr/bin/env python3
"""Rewrites the converted rule entries of conformance/cases/status.luau from the status golden data.

For every rule in tests/support/status_rule_slices.luau that has a golden record the entry gets `golden = "<tag>"` (or "db:NAME") and the
`given` / `expected` literals derived from that legacy record (printed by tests/print_status_rule_cases.luau):

    python3 tools/bundle_luau.py tests/print_status_rule_cases build/print_status_rules.luau
    luau build/print_status_rules.luau > build/print_status_rules.txt
    python3 conformance/golden_gen/splice_status_cases.py build/print_status_rules.txt
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = ROOT / "conformance" / "cases" / "status.luau"
TITLES = {
    "BR-RULESET-051": "status record: legacy id, states, calc flags, flags, fail / end lists, min rate / duration, script flag, as the real loader holds them",
    "BR-RULESET-065": "stat contributions in code order; early-return override (Harmonize); clamp 0..65535",
    "BR-RULESET-066": "DEF / DEF2: override statuses first, then code order; PRE clamps def2 to >= 1 (the early returns of Berserk / Eternal Chaos are not clamped)",
    "BR-RULESET-067": "speed model: 100 - haste + slow, base speed 150 (E-4b)",
    "BR-RULESET-068": "PRE ASPD: aspd_rate minus the best of the quicken family",
    "BR-RULESET-069": "same contribution pattern for BATK, WATK, HIT, FLEE, FLEE2, CRI, MDEF, MDEF2, MAXHP ... (an arbitrary combination)",
    "BR-RULESET-071": "statuses overriding the defense element (Freeze -> Water), the attack element and the element level; size / race / range have no overriding status",
    "BR-RULESET-073": "States (NoMove, NoCast, NoAttack) computed from the active set",
    "BR-RULESET-075": "scripted statuses contribute via script (Speedup0: bonus bSpeedRate, getstatus(SC_SPEEDUP0, 1))",
}
EXAMPLES = {"BR-RULESET-067": '"E-4b"'}

picked = {}
for line in pathlib.Path(sys.argv[1]).read_text().splitlines():
    parts = line.split("\t")
    if len(parts) == 4:
        picked[parts[0]] = parts[1:]
    elif len(parts) == 2:
        sys.exit(f"{parts[0]}: no golden record found")

src = CASES.read_text()
pat = re.compile(r'(\t\{ rule = "(BR-RULESET-\d+)", fn = "(\w+)", mode = "(\w+)", pri = "(\w+)", title = ")(.*?)(",\n)\t  example = (.*?),\n(?:\t  golden = .*?,\n)?\t  given = (.*?),\n\t  expected = (.*?) \},\n', re.S)


def sub(m):
    rid = m.group(2)
    if rid not in picked:
        return m.group(0)
    golden, given, expected = picked[rid]
    title = TITLES.get(rid, m.group(6))
    example = EXAMPLES.get(rid, m.group(8))
    return (f'\t{{ rule = "{rid}", fn = "{m.group(3)}", mode = "{m.group(4)}", pri = "{m.group(5)}", title = "{title}",\n'
            f'\t  example = {example}, golden = {golden},\n\t  given = {given},\n\t  expected = {expected} }},\n')


out, n = pat.subn(sub, src)
CASES.write_text(out)
print(f"spliced {len(picked)} rule entries into {CASES}")
