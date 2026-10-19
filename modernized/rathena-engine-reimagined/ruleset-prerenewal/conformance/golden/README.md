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

`status_pre.jsonl.gz` is the same for the STATUS calculation (13,100 cases, 699 status database records, the legacy constants): one JSON object per line, first
`{"t":"consts"}` (constants, `battle.conf` defaults, script constants, sc numbers, which skills the PRE db has), then `{"t":"db"}` (the status database as the real loader
holds it: states, calc flags, flags, fail / end lists, opt, min rate / duration, script flag), then `{"t":"case"}`: `fn` (the legacy function), `ut` (`pc` / `mob`), `base`,
`unit` (the unit fields that function reads), `cfg`, `sc` (`{NAME: [val1..val4]}`) and `out` (what the real code returned; the twelve `sc.cant` flags for `state`, the battle
status for `chain`, the bonus call log for `script`). Produced by `../golden_gen/gen_status_cases.py` (see its README), byte-identical on a fresh build; expanded to
`build/golden_status_pre.luau` by `tools/build_golden.py`. Cases with a `tag` are the hand-chosen worked examples the converted rule cases are cut from
(`tests/support/status_rule_slices.luau`).
