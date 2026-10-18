# Golden traces

`weapon_pre.jsonl.gz` holds the recorded legacy results for the pre-renewal basic weapon attack: one JSON object per line with the
case inputs in the unit form the Luau pipeline consumes (`a` attacker, `t` target, `map`, `cfg` overrides, `seed`, `cat`) and
`out` = what the REAL `battle_calc_attack(BF_WEAPON, ...)` returned (`damage`, `damage2`, `type`, `div`, `flag`, `dmg_lv`), every
RNG draw in order (a number = `rnd()`, `[lo, hi, v]` = `rnd_value(lo, hi)`) and the stage probes (`[name, damage, damage2]`).

It is produced by `../golden_gen/gen_cases.py` from a patched pre-renewal build of a COPY of the legacy tree, with a fixed seed and
byte-identical reruns. Do not edit it by hand; regenerate it and re-run `golden_gen/splice_rule_cases.py` (the converted rule cases
name golden record ids). The tests expand it to `build/golden_weapon_pre.luau` (git-ignored) with `tools/build_golden.py`.

Cases keyed by rule ID in `../cases/*.luau` with `expected = nil` still await a recording (`RATHENA_GOLDEN_TRACE`, BR-RULESET-119);
a converted rule case carries `golden = <record id>` and its `given` / `expected` are derived from that record
(`tests/support/rule_slices.luau`), never typed by hand. Inputs are synthetic: no legacy data, accounts or credentials are stored.
