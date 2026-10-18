# Legacy-VM record/replay conformance harness (M0 follow-up)

Runs the **real rAthena script VM** (headless, from `src/map/script.cpp`, built from a copy of the legacy tree) and the M0 Luau
interpreter (`../legacy_interp`) on the same NPC with the same scripted answers, records the ordered host calls of both
(command, evaluated arguments, return value) plus the final variable state, and diffs the traces.

Nothing under the repository's legacy tree is modified: the hook is a patch (`patch/legacy-replay-hook.patch`) applied to a copy.

## Run

```sh
tooling/m0/replay/build_legacy.sh  $BUILD                 # copy tree + patch + cmake/ninja -> $BUILD/rA/map-server (~10 min first time)
python3 -I tooling/m0/replay/select_corpus.py            # (re)writes corpus/corpus.json (already committed)
python3 -I tooling/m0/replay/run_replay.py --build $BUILD --luau /path/to/luau --jobs 4
python3 -I tooling/m0/replay/selftest_compare.py          # comparator self-test (13 fabricated trace pairs)
```
Outputs in `$BUILD/out/`: `legacy/*.trace` (+ `.init` start-state snapshots), `luau/`, `luau-nosc/`, `report.txt`,
`report-nosc.txt`, `results*.json`, `attribution.txt`. `$BUILD/lua` is a scratch copy of `legacy_interp` with the `prep.py` data modules
(the repo does not hold `legacy_interp/data`).

## How it works

* **Legacy hook** (`patch/`, ~330 lines, `replay_hook.inc` is `#include`d at the end of `script.cpp`):
  `run_func()` calls `replay_pre()` (evaluates arguments on a copy of the stack data, honouring the buildin's `r` reference
  positions) and `replay_post()` (top of the pushed return). Only calls of the traced script (`st->oid`/`st->rid`) count.
  `replay_main()` (called right after `ScriptHost::run_oninit()`) creates a synthetic player (`fd 0`, registered in `id_db`,
  `pc_group_pc_load`), starts the NPC with `run_script()` and then does what `npc_scriptcont()` does after a client answer:
  `STOP` -> run, `CLOSE` -> end, `RERUNLINE` -> store the answer in `sd->npc_menu` / `npc_amount` / `npc_str` and re-run the line.
  `clif_script*` return early. `sleep` timers are fired synchronously. Environment variables: `RA_REPLAY_NPC`, `_ANSWERS`, `_PLAYER`,
  `_TRACE`, `_CAP`, `_LABEL`, `_DUMP_CONSTS`.
* **Scripted inputs shared by both sides** (so that a diff means a semantic difference, not noise): answers file (menu/select/prompt
  1-based, 0 = cancel, larger than the option count = last option; input number or text; exhausted: first option / 1 / "test"),
  player profile, `rand()` (LCG sequence overwritten into the return value on the legacy side), `gettime()/gettimetick()` (fixed
  2026-10-18 12:34:56, a Sunday), the integer constants (dumped from the real VM and substituted into the Luau side), and the
  start-state of `$`, `$@` and the NPC's `.` variables (snapshot taken by the legacy hook, seeded into the Luau side).
* **Luau side** (`luau/replay_trace.luau`): wraps the command table of `legacy_interp` (no change to its files) to emit the same
  format, own coroutine driver with the same interaction cap, soft stubs (`!U`) for commands `legacy_interp` lacks.
  Mode `nosc` additionally rewrites the lowered program so `&&`, `||`, `?:` evaluate all operands, as the legacy VM does.
* **Comparator** (`compare_traces.py`): `difflib` alignment on command names, per-record classification, first divergence
  per case, cascade flag, examples per class.

## Trace format (one file per run)

```
name(arg,...) [-> ret] [!F] [!U]      host call, in execution order; args: 123  "text"  &ref  @label
#end <end|cap|alive|blocked:x|error:T|...>
=<scope><var>[idx]=<value>            variables changed by the run ('$' '$@' '.' '@' '#' '##' perm), "<unset>" when removed
%param=n    +item <id>=n              player params / inventory at the end
```
Not recorded (VM internals the legacy compiler emits as calls, or lvalue accessors): `set setr jump_zero goto callsub callfunc return getarg getargcount end
getelementofarray getd setd`. Label-form `menu "a",L1,"b",L2;` is recorded as `menu("a","b")` (visible option texts).
Return absent is equal to `0`.

## Results of the first run (46 NPCs x 3 answer/player variants = 138 runs, renewal load set)

`results/report-asis.txt` (legacy_interp unchanged), `results/report-nosc.txt` (operands of `&& || ?:` all evaluated),
`results/report-asis-synthetic-constants.txt` (baseline with prep.py's synthetic enum ids), `results/attribution.txt`.
Strict trace + end-state equality: 94/138 (68.1%) as is, 102/138 (73.9%) with all-operand evaluation, 84/138 (60.9%) with the
synthetic constant ids of `prep.py`. Every failing case is attributed: interpreter semantics 11, missing world state 30, command coverage 3.
