# Legacy-VM record/replay conformance harness (M0 follow-up)

Runs the **real rAthena script VM** (headless, from `src/map/script.cpp`, built from a copy of the legacy tree) and the M0 Luau
interpreter (`../legacy_interp`) on the same NPC with the same scripted answers, records the ordered host calls of both
(command, evaluated arguments, return value, failure flag) plus the final variable state, and diffs the traces.

Nothing under the repository's legacy tree is modified: the hook is a patch (`patch/legacy-replay-hook.patch`) applied to a copy.
Results and the list of deviations we keep: bottom of this file; raw reports: `results/`.

## Run

```sh
B=/tmp/replay-build                                          # any scratch directory
RA_PRERENEWAL=0 tooling/m0/replay/build_legacy.sh  $B        # copy tree + patch + cmake -> $B/rA/map-server        (~10 min, renewal)
RA_PRERENEWAL=1 tooling/m0/replay/build_legacy.sh  $B/pre    # same with `#define RENEWAL` commented out in the COPY (pre-renewal)
L=/path/to/luau
python3 -I tooling/m0/replay/run_replay.py --build $B --luau $L --jobs 4                                  # corpus.json: 46 NPCs x 3 (renewal)
python3 -I tooling/m0/replay/run_replay.py --build $B --luau $L --corpus corpus/corpus_wide.json --set re --report-name wide
python3 -I tooling/m0/replay/run_replay.py --build $B/pre --luau $L --corpus corpus/corpus_wide.json --set pre --report-name wide
python3 -I tooling/m0/replay/run_replay.py --build $B --luau $L --synthetic --set re --report-name synth   # synthetic.txt: semantics pinned one by one
python3 -I tooling/m0/replay/attribute.py --out $B/out --name wide                                         # classify the failures
python3 -I tooling/m0/replay/smoke.py --build $B --build-pre $B/pre --luau $L                              # regression smoke test (~1 min)
python3 -I tooling/m0/replay/selftest_compare.py                                                           # comparator self-test (15 fabricated trace pairs)
```
`--skip-legacy` / `--skip-luau` re-run one side only (legacy traces are kept in `<build>/out/legacy`), `--interp DIR` tests another copy of the
interpreter sources (used for the "before" numbers below), `--only n05,n17` restricts the cases. Outputs in `<build>/out/`: `legacy/*.trace`
(+ `.init` start-state snapshots), `luau/`, `report[-NAME].txt`, `results[-NAME].json`, `attribution[-NAME].txt`, `world_*` (data dumped from the real VM).
`<build>/lua` is a scratch copy of `legacy_interp` + the `prep.py` data modules (the repo does not hold `legacy_interp/data`); the interpreter sources
are re-synced from `--interp` (default `../legacy_interp`) on every run.

Corpora (`corpus/`): `corpus.json` (46 NPCs, renewal, first run), `corpus_wide.json` (160 NPCs: 80 renewal + 80 pre-renewal, `select_corpus_wide.py`),
`corpus_hunt.json` (600 NPCs, 300 per set, seeded random from the eligible pool, disjoint from the other two: `select_corpus_wide.py --random 300`),
`synthetic.txt` (11 hand-written NPCs), `smoke.json` (the fixed regression subset). Every NPC runs 3 times (4 for synthetic): `a` = every
menu is the first option and the player is poor (0 zeny, level 1), `b` = every menu is the last option and the player is rich, `c` = mixed answers
(`d` = every menu cancelled, synthetic only).

## How it works

* **Legacy hook** (`patch/`, `replay_hook.inc` is `#include`d at the end of `script.cpp`): `run_func()` calls `replay_pre()` (evaluates arguments on a copy of
  the stack data, honouring the buildin's `r` reference positions) and `replay_post()` (top of the pushed return, `!F` when the buildin returned
  `SCRIPT_CMD_FAILURE`). Only calls of the traced script count. `replay_main()` (called right after `ScriptHost::run_oninit()`) creates a synthetic
  player (`fd 0`, registered in `id_db`, standing on the NPC, `equip_index[]` = -1 as `pc_authok()` leaves it, group 0), starts the NPC with `run_script()`
  and does what `npc_scriptcont()` does after a client answer. `clif_script*` return early. `sleep` timers are fired synchronously. The end state is the
  delta of `$`, `$@`, the NPC's `.` and the character variables, the params, and the inventory aggregated per item id. The achievement scratch registers
  `ARG0..ARG9` (written by `achievement.cpp` whenever `getitem` & co fire an objective) are not script state and are not dumped. Environment variables:
  `RA_REPLAY_NPC`, `_ANSWERS`, `_PLAYER`, `_TRACE`, `_CAP`, `_LABEL`, `_DUMP_CONSTS`. `patch/regen_patch.py` rewrites the patch's new-file section from an edited copy.
* **Scripted inputs shared by both sides** (a diff means a semantic difference, not noise): answers, player profile, `rand()` (an LCG over the call
  counter overwrites the legacy return value; the interpreter takes the same sequence through its `env.rand` hook but keeps its own argument
  validation), `gettime()/gettimetick()` (fixed 2026-10-18 12:34:56, a Sunday), the integer constants (dumped from the real VM and substituted), and the start
  state of `$`, `$@` and the NPC's `.` variables (snapshot taken by the hook, seeded into the Luau side).
* **World data from the real VM** (`run_replay.py`, never invented): a generated probe NPC is installed *temporarily into the build copy* and run through the
  same hook; its trace is turned into `world_data.luau`, which the harness hands to the interpreter through its data-provider interface
  (`legacy_interp/world.luau`): every item name and AegisName (`getitemname`), the battle config (`getbattleflag`), castle maps (`getcastledata`), loaded
  maps (`mobcount`, `warp`, `getmapusers`; the pre-renewal map cache has fewer maps), plus the shop-NPC names read from the NPC files (`callshop`). Static
  database queries whose domain is too large to dump (`getiteminfo`, `getmonsterinfo`, `mesitemlink`, `getitemslots`, ...) are **demand-driven**: the Luau run
  logs the queries it could not answer, the legacy VM answers exactly those (`world_lookup.json` cache), the Luau run repeats. These answers come from the
  real VM but for the keys the interpreter asked, so they are an oracle for *data*, not for control flow.
* **Luau side** (`luau/replay_trace.luau`): wraps the command table of `legacy_interp` (no change to its files) to emit the same format, its own coroutine
  driver with the same interaction cap, soft stubs (`!U`) for commands `legacy_interp` lacks.
* **Comparator** (`compare_traces.py`): `difflib` alignment on command names, per-record classification, first (root) divergence per case, cascade flag,
  examples per class. A `!F` on one side only is a mismatch (`legacy_failure` / `luau_failure`); a failure both sides agree on is not. Cases whose NPC the real server
  did not instantiate (its map is not in the build's map cache: `npc-not-found` from the hook) are **excluded and reported separately**, never counted as pass or fail.
* **Attribution** (`attribute.py`): root divergence -> interpreter bug | missing world state | command coverage | harness artifact | documented deviation.
  The tables are short and explicit (`WORLD`, `DEVIATIONS`): a divergence is a bug unless a rule says otherwise.
* **Synthetic NPCs** (`corpus/synthetic.txt`): pin semantics the corpus exercises rarely (switch leftovers and fall-through, no short-circuit, order of effects,
  by-reference arguments, int64 operators, string/array commands, negative array index, instance variables, loops, `callfunc`/`getd`...). For the legacy side the
  file is installed into the copy for the duration of the run; the Luau side parses the same file.

## Trace format (one file per run)

```
name(arg,...) [-> ret] [!F] [!U]      host call, in execution order; args: 123  "text"  &ref  @label
#end <end|cap|alive|blocked:x|error:T|...>
=<scope><var>[idx]=<value>            variables changed by the run ('$' '$@' '.' '@' '#' '##' perm), "<unset>" when removed
%param=n    +item <id>=n              player params / inventory (aggregated per item id) at the end
```
Not recorded (VM internals the legacy compiler emits as calls, or lvalue accessors): `set setr jump_zero goto callsub callfunc return getarg getargcount end
getelementofarray getd setd`. Label-form `menu "a",L1,"b",L2;` is recorded as `menu("a","b")`. Return absent is equal to `0`. `!F` = the buildin returned
`SCRIPT_CMD_FAILURE`; `!U` = unsupported command in `legacy_interp` (soft stub).

## Regression smoke test

`smoke.py` re-runs the 10 NPCs of `corpus/smoke.json` (5 renewal, 1 pre-renewal, 4 synthetic = 34 cases) on both sides in about a minute and exits 1 when the
strict-pass count drops below the recorded baseline `smoke_baseline.json` or when a case that passed at record time fails. `--record` rewrites the
baseline (review the diff first). Without `--build-pre` the pre-renewal part is skipped.

## Results

See `results/` (`report-*.txt`, `attribution-*.txt`) and the table in `../legacy_interp/RESULTS.md` ("Replay-driven corrections"). Summary at the end of this file.

## Results (final run of this change; legacy VM = renewal build and pre-renewal build of the same tree)

Strict = ordered host calls with evaluated arguments, returns and failure flags, plus the end state (variables, params, inventory) identical.
*Before* = the interpreter as committed at the start of this change (`git show 0658b07:.../legacy_interp`), run through the same harness on the same legacy traces.

| Corpus (runs = NPCs x variants) | Before | After |
|---|---|---|
| `corpus.json` renewal, 46 NPCs, 138 runs | 95 / 138 (68.8%) | **138 / 138** |
| `corpus_wide.json` renewal, 80 NPCs, 240 runs | 183 / 240 (76.2%) | **240 / 240** |
| `corpus_wide.json` pre-renewal, 80 NPCs, 240 runs | 190 / 240 (79.2%) | **240 / 240** |
| `corpus_hunt.json` renewal, 300 NPCs, 900 runs | 809 / 900 (89.9%) | **900 / 900** |
| `corpus_hunt.json` pre-renewal, 300 NPCs (7 NPCs / 21 runs not instantiated by the real server, excluded), 879 runs | 816 / 879 (92.8%) | **876 / 879** (99.7%) |
| `synthetic.txt`, 11 NPCs x 4 variants, renewal and pre-renewal (2 x 44 runs) | 8 / 44 each | **44 / 44** each |
| total (2,485 runs) | 2,109 (84.9%) | **2,482 (99.9%)** |

(The first run of this harness measured 94 / 138 on `corpus.json`; the one-case difference comes from the hook fixes below. After only the three
interpreter fixes asked for first - no short-circuit, statements before the first `case`, `$@__SWn_VAL` - the same corpus reached 105 / 138.)
53,565 recorded host calls were compared (the number of calls of the legacy VM equals the interpreter's in every report); 1,445 of the runs reach a blocking interaction.

Remaining failures (3 runs, 1 NPC): **missing world state** - `Robots#kh1` (`npc/quests/kiel_hyre_quest.txt:6282`, pre-renewal): `monster` stores the runtime block id of the
spawned monster in `$@mobid`; the id comes from the server's object allocator (`110013151` here) which the interpreter does not model. No interpreter bug, no command
gap and no harness artifact remains in the corpora (`results/attribution-after-hunt-pre.txt`; every other report is 100%).

The hunt corpora were added to look for the next bug after the first two corpora passed: they found, in order, the missing `insertchar`/`delchar`/`setchar`, `strnpcinfo(2)`, the
`input` return/clamp rule, `checkre`, `mesitemicon`, barter shops for `callshop`, `makeitem`/`refineui` and the remaining no-op client commands, and the non-ASCII NPC names
(harness artifact: the environment variable carrying the name was re-encoded). Diminishing returns: after the pre-renewal hunt the renewal hunt of 900 runs found 3 more (`checkre`, `mesitemicon`, barter names for `callshop`), all of them data/command gaps, no control-flow bug.

### Deviations we keep (each is a documented difference from the legacy VM, none is hidden by the comparator)

1. **`switch` compat flag** `lower.switchCompat` (default **on**) reproduces the legacy compare chain and its `$@__swN_val` leftovers; off = table dispatch without the global (still runs the statements before the first `case`).
2. **Numbers are IEEE doubles**: exact below 2^53, no int64 wrap/overflow detection (the legacy `safe_addition` ends the script on overflow), no int32 truncation of `script_getnum` arguments (only the ones modelled: `getitem` amount as uint16, `strtol`/`axtoi` caps).
3. **Operand order inside one expression**: command calls and `callsub`/`callfunc` run left to right (verified), variables are read at the operator after the calls to their right (legacy: lazy C_NAME) - but an *assignment* embedded in an expression is not reordered (`(.@a = 1) + callsub(...)`; not seen in the corpora).
4. **Parser is more lenient**: `FunctionName(args)` call syntax for `function script` functions parses (the legacy parser rejects it), a few statement terminators are accepted (see `run_parse` output).
5. **World model is minimal and empty by design**: no party, guild, pet, homunculus, mercenary, mount/cart/riding, status changes, equipment (hosts may fill `player.equip`), no instances (`instance_*` fail and end the script as outside an instance), no timers/events/other players/`attachrid`. Monster spawns are counted per event name only (no object ids, no mob DB check).
6. **Inventory is `item id -> amount`**: no weight/slot limits, no bound/refine/card attributes (`getitem2/3/4`, `getitembound*` ignore them); the legacy `pc_additem` failure cases are not modelled.
7. **`sscanf`** supports `%s %d %i %u %x %o %c %[...] %n` with widths and `*`; floats and positional forms are not. `sprintf` supports `d i u o x X c s e E f F g G` with flags/width/precision.
8. **`rand`** takes its source from the host (`env.rand`); the default is an LCG, not the server's RNG. Failure rules (empty range ends the script) match.
9. **Budgets**: ops (10,000 per yield slice, 500,000 in the harness) and gotos (2048) are interpreter policy; the legacy VM counts stack tokens and jumps differently. `MAX_DEPTH` 2048 call frames.
10. **Unsupported commands**: the interpreter raises `UNSUPPORTED` (the harness maps it to a soft `!U` stub and the comparator reports `missing_command`). 427 of the 671 builtins have no implementation; none of them is reached by the 2,485 runs of the final corpora (a hit would show as `missing_command`).
11. **World data** (item names, battle config, castles, maps, shops, DB lookups) comes from the real VM, not from the interpreter: the interpreter ships an empty default provider, so item/mob/skill data must be supplied by the host (see `legacy_interp/world.luau`). Static DB queries are answered by a demand-driven oracle of the real VM (an oracle for data, not for control flow).
12. **Not covered at all**: timers (`OnTimer`, `addtimer`), `OnInit`/`OnClock`/touch/mob-death events, `sleep` scheduling, multiple players, party/guild flows, `duplicate()` variable sharing. The corpora select entry flows of single-player NPCs.

## Files of this directory
`run_replay.py` (driver), `compare_traces.py`, `attribute.py`, `selftest_compare.py`, `smoke.py` + `smoke_baseline.json`, `select_corpus.py`, `select_corpus_wide.py`, `build_legacy.sh`,
`patch/` (hook + `regen_patch.py`), `luau/` (Luau side), `corpus/` (inputs), `results/` (`report-before-*`, `report-after-*`, `attribution-after-hunt-pre.txt`; the three `report-asis*`, `report-nosc` and `attribution.txt` are the first run of this harness and are kept for the record).
