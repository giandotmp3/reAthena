#!/usr/bin/env python3
"""Rewrites the converted rule entries of conformance/cases/damage.luau from the golden data.

For every rule in tests/support/rule_slices.luau the entry gets `golden = <record id>` and the `given` / `expected`
literals derived from that legacy record (printed by tests/print_rule_cases.luau). Run after (re)generating the golden data:

    python3 tools/bundle_luau.py tests/print_rule_cases build/print_rules.luau
    luau build/print_rules.luau > build/print_rules.txt
    python3 conformance/golden_gen/splice_rule_cases.py build/print_rules.txt
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = ROOT / "conformance" / "cases" / "damage.luau"
TITLES = {
    "BR-RULESET-030": "element application: non-player basic attacks skip the table while attack_attr_none covers them (NK_IGNOREELEMENT is the skill-side twin, not covered)",
    "BR-RULESET-041": "map-mode rates: GvG basic attacks x80% short / long, min 1 (skill x60% and PK rates not covered)",
    "BR-RULESET-032": "attr_recover=0 keeps a negative element result from healing: damage is cut to 0",
}
EXAMPLES = {"BR-RULESET-020": '"E-3"', "BR-RULESET-031": '"E-1"', "BR-RULESET-037": '"E-6"'}

picked = {}
for line in pathlib.Path(sys.argv[1]).read_text().splitlines():
    parts = line.split("\t")
    if len(parts) == 4:
        picked[parts[0]] = parts[1:]
    elif len(parts) == 2:
        sys.exit(f"{parts[0]}: no golden record satisfies the rule's find()")

src = CASES.read_text()
pat = re.compile(r'(\t\{ rule = "(BR-RULESET-\d+)", fn = "(\w+)", mode = "(\w+)", pri = "(\w+)", title = ")(.*?)(",\n)\t  example = (.*?),\n\t  given = (.*?),\n\t  expected = (.*?) \},\n', re.S)


def sub(m):
    rid = m.group(2)
    if rid not in picked:
        return m.group(0)
    gid, given, expected = picked[rid]
    title = TITLES.get(rid, m.group(6))
    example = EXAMPLES.get(rid, m.group(8))
    return (f'\t{{ rule = "{rid}", fn = "{m.group(3)}", mode = "{m.group(4)}", pri = "{m.group(5)}", title = "{title}",\n'
            f'\t  example = {example}, golden = {gid},\n\t  given = {given},\n\t  expected = {expected} }},\n')


out, n = pat.subn(sub, src)
CASES.write_text(out)
print(f"spliced {len(picked)} rule entries into {CASES}")
