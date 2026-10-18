Copies of `modernized/rathena-reimagined/map-rules/tests/golden/defense_{pc,mob}_pre_re.jsonl`, captured from the legacy server by
`tools/golden/capture.sh` through the `RATHENA_GOLDEN_TRACE` hook in `battle_calc_defense_reduction` (battle.cpp:4910). They are a second,
independent recording of the DEF / soft-DEF step (including the RNG spread) and are replayed by `tests/acceptance/weapon_golden_test.luau`
against `weapon.vit_def` / `weapon.def_reduce`. Read-only data; do not edit.
