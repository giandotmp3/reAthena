# M0(b) results: Luau-hosted parser + flat-op interpreter for the legacy NPC dialect

Verdict: **GO-WITH-CHANGES**. Method, raw outputs (`out/*.txt`) and rerun steps are in `README.md`.
All times are CPU seconds on this 4-core box with the stock (non-`--codegen`) Luau VM unless stated.
Corpus is the repo at `/home/user/reAthena`. Load sets follow `npc/re/scripts_main.conf` (835 files) and `npc/pre-re/scripts_main.conf` (527 files), the same counts as the spec.

## 1. Parse success over the load sets

| | renewal | pre-renewal |
|---|---|---|
| files | 835 | 527 |
| lines | 721,587 | 383,586 |
| declaration splitter errors | 0 | 0 |
| `script` bodies | 9,219 | 4,386 |
| `function script` bodies | 149 | 96 |
| `duplicate(...)` (body shared, not re-parsed) | 10,498 | 5,427 |
| non-script headers counted and skipped | 5,805 mapflag, 3,889 monster, 4,105 warp, 347 shop (45 marketshop) | 3,595 mapflag, 3,911 monster, 3,120 warp, 186 shop |
| **parse failures** | **0 / 9,368 (100%)** | **0 / 4,482 (100%)** |
| lowering failures | 0 | 0 |
| tokens | 2,463,365 | 1,214,525 |
| body text | 21.1 MB | 10.4 MB |
| splitter time | 0.49 s | 0.24 s |
| tokenize + parse time | 2.08 s (about 10 MB/s) | 1.03 s |
| lowering time | 0.66 s | 0.32 s |
| AST memory (retained, `collectgarbage("count")` delta, sources already loaded) | 251.5 MB (107 B/token) | 128.6 MB (111 B/token) |
| lowered programs memory (flat ops, AST dropped) | 302.6 MB (129 B/token), 599,147 ops | 157.6 MB (136 B/token), 314,614 ops |

Failures grouped by cause, final parser: none. How the first drafts failed is worth knowing, because the final 100% only holds if these rAthena behaviours are copied deliberately:

- 9 bodies failed at first with `unexpected_token: &&` / `||`, e.g. `if (countitem(7201) > 499) && (countitem(7200) > 9 && moza_tal == 1) {` (`npc/quests/newgears/2006_headgears.txt:615`, `npc/jobs/1-1e/taekwon.txt:29`). That is **valid** rAthena: `if`/`while`/`switch` call `parse_expr` starting at the `(`, so the condition is the whole expression, not "parenthesised expr". Fixed in the parser.
- 3 bodies rely on a **parser quirk**: after an assignment statement `parse_line` returns `parse_syntax_close(p2 + 1)`, i.e. it blindly skips one character as if it were `;`. The corpus contains `for ( .@i = 0, .@i < .@size; ...)` (`npc/re/merchants/novice_vending_machine.txt:68`), an extra `)` (`npc/quests/quests_moscovia.txt:9923`) and `.@type = getarg(0):` (`npc/re/quests/quests_15_2.txt:2414`). The parser accepts `,` `)` `:` in that slot (counters in `parser.quirks`: `)=1 ,=1 :=1` renewal, `)=1` pre-renewal). A cleaner front end would have to either replicate this or fix the three scripts.
- Arity: rAthena treats too few and too many arguments (per `BUILDIN_DEF` spec) as hard errors. The parser enforces both (`mes;`, `close "x";` rejected in `t_neg.luau`); lenient paren-less zero-arg calls in expression position: 0.
- 1 lowering failure at first: `case EQI_SHADOW_ARMOR` (constant extracted wrongly because the header has `export_constant(EQI_SHADOW_ARMOR );`). Fixed in `prep.py`, now 0.

Not verified: parity with the C++ parser. There is no built map-server here, so "accepted" means my parser accepts it, not that the AST or constant resolution equals rAthena's. Map-existence and coordinate checks from `npc_parsesrcfile` were not reproduced (no map DB).

## 2. Worst-case files, driven end to end

Driver: for every NPC in the file, for each entry point (main body plus every `On*` label) and for two player profiles (`poor`, `rich`): (a) strict DFS over menu/select/prompt answers (option 1, then 2, ... by replay, budget 200 runs, depth cap 24 choices, 300 yields); (b) the same in soft mode where an unsupported command records and returns 0, to inventory everything behind the first unsupported one; (c) a **branch-flip DFS** where every `JZ/JNZ/switch` is also a choice point (option 1 = natural outcome, at most 3 flips per run, budget 1500 runs). (c) is my addition because menu-only DFS barely moves in state-dependent scripts (see enchan_mal). "End states" = distinct (NPC, entry, profile, end kind, final pc/error). "Frame depth" counts the entry frame, so callsub nesting = depth - 1.

The five requested files, command set `base+ext` (base = requested ~40 + recording stubs for the next ~60 by usage; ext = string functions and more stubs). Numbers for the base set alone are in `out/worst_*_base.txt` and in the unsupported column.

| file | lines | bodies parsed | NPCs / entries | DFS runs (natural) | distinct end states natural / +flip | op coverage natural -> +flip | max frame depth | drive time |
|---|---|---|---|---|---|---|---|---|
| `npc/re/merchants/enchan_mal.txt` | 686 | 1/1 | 1 / 1 | 39 | 6 / 21 | 27.2% -> 71.5% | 3 | 0.30 s |
| `npc/re/guild/invest_main.txt` | 1,672 | 12/12 | 12 / 48 | 1,018 | 128 / 268 | 41.9% -> 78.1% | 3 | 3.3 s |
| `npc/re/quests/quests_16_1.txt` | 18,841 | 254/254 | 254 / 363 | 1,596 | 754 / 2,198 | 13.8% -> 94.8% | 2 | 4.2 s |
| `npc/guild2/agit_main_se.txt` | 1,847 | 11/11 | 11 / 52 | 212 | 106 / 255 | 16.2% -> 86.4% | 2 | 0.67 s |
| `npc/cities/prontera.txt` | 730 | 20/20 | 20 / 20 | 1,904 | 64 / 140 | 79.2% -> 95.5% | 1 | 5.0 s |

(parse + lower for each file is 0.004 to 0.19 s; `quests_16_1.txt` 0.19 s.)

Unsupported commands encountered (counts are DFS runs that ended in the typed `UNSUPPORTED` error for the strict pass; the soft-pass number in brackets is calls reached):

| file | base set | base+ext |
|---|---|---|
| enchan_mal | `getequipisequiped`=4 | `getitemslots`=2 (25), `getitem2` (10) |
| invest_main | `agitcheck`=14, `implode`=4 (55), `charat`=2, `explode`=2, `getgmlevel`=2, `getmapxy` (2), `logmes` (2); 10/12 NPCs disabled | none |
| quests_16_1 | `achievementinfo`=14, `eaclass`=14, `sscanf`=6, `getgmlevel`=6, `replacestr`=4, `getmapxy`=2, `basicskillcheck`=2, `mesitemlink`=2, `readbook`=2, `npcskill`=1 (+ `achievementupdate`, `logmes`, `openstorage`, `unitwarp` in soft pass); 25/254 NPCs disabled | none |
| agit_main_se | `charat`=44 (98 reached), `substr`=12 (32), `awake`, `getguildname`, `guardian` (50), `setcastledata`; soft pass adds `agitcheck2`, `checkwall`, `copyarray`, `getcastlename`, `maprespawnguildid`, `setunitdata` (24), `setwall`; 8/11 NPCs disabled | none |
| prontera | none | none |

Other driver observations:
- No `INTERNAL` (interpreter bug) errors in any of these runs, nor in the full-corpus run below.
- `agit_main_se.txt`: `freeloop(1)` executed (8 times in the soft passes) and lifts both budgets; 44 `donpcevent` calls and 4 `monster` calls were recorded; no runaway loop. Its one remaining non-unsupported end is `GETARG_OUT_OF_RANGE` at line 1447 (an `On*` label entered without the arguments its caller would pass: a driver artifact).
- `prontera.txt` has no labels and no callsub; every menu loop returns to the menu, so the DFS ends in `DEPTH_CAP` (1,092 runs) by design. Its 20 NPCs are mostly `mes/next/select` loops: 113,694 `mes` and 53,806 `next` calls recorded.
- `enchan_mal.txt` is the best illustration of the driver's weakness: 188 `callsub` sites hang off a `switch (getiteminfo(...))` over equipment ids, so natural DFS (menus only) reaches 4.4% of ops in base mode and 27.2% with `ext` (the `rich` profile returns an equipped dagger); the branch oracle is what reaches 71.5%.

Why these five and what the density ranking says (`out/density.txt`, AST-based, 987 files): the corpus has only **107 `goto`**, 2,196 `callsub`, 1,843 `callfunc`, **5 `menu`** (label form), 11,850 labels, 14 `freeloop`, 216 `getd`, 138 `setd`, 4,964 `donpcevent`. Rank by goto + callsub + labels: `invest_main` 6th, `enchan_mal` 13th, `quests_16_1` 26th, `agit_main_se` 107th, `prontera` 746th (0 goto, 0 callsub, 0 labels: it is a big city file, not a control-flow-dense one). The truly densest files are `quests_juperos` (403 labels), `TempleOfDemonGod`, `FacewormsNest`, `SarahAndFenrir`; the only heavy `goto` users are `npc/quests/cooking_quest.txt` (54) and `npc/other/marriage.txt` (18). I drove those too (`out/worst_cooking_quest_*`, `worst_marriage_*`, `worst_TempleOfDemonGod_*`, `worst_FacewormsNest_*`, `worst_quests_juperos_*`): all parse, no internal errors, coverage with the oracle 83% to 96%, max frame depth 3.

### Whole-corpus drive (every script body, small budgets: 6 menu runs + 12 flip runs per entry and profile)

| | renewal, base | renewal, base+ext | pre-renewal, base | pre-renewal, base+ext |
|---|---|---|---|---|
| script bodies driven | 9,219 | 9,219 | 4,386 | 4,386 |
| entry points driven | 20,642 | 20,642 | 9,937 | 9,937 |
| runs (natural / flip) | 92,200 / 127,174 | 92,899 / 127,182 | 44,853 / 60,890 | 45,291 / 60,878 |
| wall time | 17.6 s | 19.0 s | 6.3 s | 6.7 s |
| `INTERNAL` errors | 0 | 0 | 0 | 0 |
| bodies that hit an UNSUPPORTED command | 1,206 | 669 | 518 | 378 |
| op coverage natural / +flip | 25.3% / 65.7% | 25.4% / 65.7% | 25.4% / 62.4% | 25.4% / 62.4% |
| max frame depth | 5 (`npc/kafras/cool_event_corp.txt`) | 5 | 5 | 5 |
| host-recursive nested calls executed | 0 | 0 | 0 | 0 |

Typed non-unsupported ends in natural runs: `DEPTH_CAP` (menu loops, by design) and `GETARG_OUT_OF_RANGE` (event labels called without args). Static command coverage (`out/usage.txt`): 570,483 call sites, 401 distinct called names (355 builtins, 46 user/function-script/inline-function names; the spec's 388 counts differently). The base table handles 135 of the 401 names; unsupported call sites are 5,218 (0.91%) and 1,547 of 9,518 unique bodies (16.3%) contain at least one. With ext: 203 names handled, 2,738 call sites (0.48%), 993 bodies (10.4%).

Most-hit unsupported commands in the corpus (dynamic, NPCs reaching each, renewal base): `getmapxy` 127, `getgmlevel` 97, `logmes` 97, `flagemblem` 84, `getbattleflag` 74, `getunitdata` 55, `implode` 54, `setunitdata` 52, `callshop` 51, `setunittitle` 51. Static top by call sites: `setunitdata` 159, `getskilllv` 142, `callshop` 142, `getequipid` 141, `getbattleflag` 138.

## 3. Constructs that cannot (or do not trivially) be represented

`out/constructs.txt` (lowered corpus, 9,518 unique bodies). Nothing failed to lower, nothing is unrepresentable in the flat design. These need attention:

1. **callsub/callfunc inside `?:`, `&&`, `||`: 111 sites** (104 `callfunc`, 6 `f_getpositionname`, 1 `f_ischarm`). Everything else (971 renewal + 333 pre-renewal call sites in expressions) is hoisted into ops before the expression, so frames stay on the heap. These 111 cannot be hoisted without changing short-circuit semantics, so the evaluator calls `invoke` recursively on the **host stack**. Measured limit: a recursive chain of such calls works to depth 4,000 and raises a typed `HOST_STACK_OVERFLOW` at 5,000 (statement-position recursion on heap frames is capped by `MAX_DEPTH`=2048 here and raises `CALL_DEPTH`; the cap is a constant). First sites: `npc/other/Global_Functions.txt:518`, `npc/merchants/advanced_refiner.txt:194`, `npc/merchants/refine.txt:813,814,935,936`, `npc/quests/seals/mjolnir_seal.txt:2045,2264`, `npc/re/instances/FridayDungeon.txt:599`. So the design claim "no Luau recursion limit" holds for statement-level calls but not for these; a stack-machine for expressions (or compiling conditionals to jumps) removes it.
2. **Labels defined inside a nested block (if/loop/switch/`{}`): 25.** In a compile-to-Luau design these are impossible to `goto` into; here they are plain pcs. Examples: `npc/quests/quests_ayothaya.txt:547`, `quests_brasilis.txt:190,1733`, `quests_lighthalzen.txt:7669,9623,9747`, `quests_rachel.txt:7445`, `npc/jobs/2-2/sage.txt:712`, `npc/re/instances/CorOperation.txt:311,472`, `npc/re/quests/eden/eden_tutorial.txt:22`. `npc/re/merchants/enchan_verus.txt:777` is a typo label `dfault:` inside a switch (treated as a label, as in rAthena).
3. **Dynamic goto/callsub targets: 0**, matching the spec. Inline `function name { }` definitions: 27 (e.g. `npc/other/monster_race.txt:1682`, `npc/other/turbo_track.txt:205,1924,2065`, `npc/quests/okolnir.txt:369,824`); lowered as skip-jump + labelled body + return and called with callsub semantics.
4. **Reference semantics through calls** (found by corpus drive, not in the spec): `set getarg(0), v;`, `getelementofarray(getarg(2), .@i)`, `set getvariableofnpc(.var,"NPC"), v;`, `setarray getd("$x"+...+"[0]"), ...`. Arguments that are bare variables are passed as references (`argrefs`) and `getarg`/`getvariableofnpc`/`getd` are accepted as lvalues in `r` argument positions. Examples: `npc/re/instances/WernerLaboratoryCentralRoom.txt:464`, `npc/re/instances/WaveMode.txt:28`, `npc/re/guild3/agit_main_te.txt:678`, `npc/quests/newgears/2004_headgears.txt:2474`, `npc/guild2/agit_main_se.txt:833`. Before this was added, the renewal corpus drive ended 27 runs in `BAD_REF` on exactly these forms.
5. Switch/case: 5,088 `switch`, 20,186 `case` entries lowered to a hash dispatch (`SWITCH` op) instead of rAthena's chain of compares. Observable difference: none for constant case labels (rAthena requires them), but case labels need real constant values, and my `consts.luau` has **synthesized values** for constants whose numeric value is a C++ enum (see risks).

### Verification of the ".@ locals and labels share state across the whole script" claim

Confirmed, and trivially: a `.@` variable is a key in the current frame's `locals` table; labels are just pcs in the same op array, so `goto` / fall-through / `switch` jumps keep the frame; `callsub` / `callfunc` push a new frame with fresh `locals` (and `.@` of the caller is untouched; test "callsub gets fresh .@ frame"). `t_semantics.luau` (14 tests, all pass) covers: shared locals across goto/labels, callsub frames and return values, precedence/associativity/int division and modulus signs/string concatenation, switch fall-through + default in the middle + continue inside switch in a loop, arrays and all nine sigil scopes, by-reference `getarg`, menu numbering with empty and grouped options and the `-` label, select/prompt, op and goto budgets, `freeloop`, typed UNSUPPORTED, heap-frame depth 2000, `CALL_DEPTH` beyond the cap.

The population where this matters: **246 bodies (401 variable names) use a `.@` variable in 2 or more label regions** (anything between two labels). Only 1 body does so within one frame family once regions that start at a `callsub` target or an `On*` event label are excluded (`npc/quests/quests_brasilis.txt` [Lucia#brasilis] `.@nqstate1/2`), so in practice the liveness-across-labels problem that sank compile-to-Luau-locals is concentrated in very few scripts, but the flat design pays nothing for it. This analysis is an approximation (no CFG); it is an upper bound for the second number being small.

## 4. Per-statement overhead (tight loops, 2M iterations, best of 5)

Interpreted unit = one flat op (JZ, JMP, EXPR statement, CALLSUB, RETURN, SWITCH), each including its expression evaluation. The Luau CLI has no `debug.setinterruptcallback`, so "counter" is the interpreter's own per-op ops/limit check (`interp.luau` vs the generated `interp_nocount.luau`).

| loop | counter on | counter off | overhead |
|---|---|---|---|
| `for` with `.@s += .@i & 7` (4 ops/iter) | 204.5 ns/op | 190.2 ns/op | +7.5% |
| `while` with 3 statements (5 ops/iter) | 277.9 | 257.8 | +7.8% |
| `$x = $x + 1` global store (4 ops/iter) | 183.4 | 159.2 | +15.2% |
| `callsub` + `return` (5 ops/iter) | 290.0 | 237.1 | +22.3% |
| `switch` dispatch (6 ops/iter) | 155.3 | 129.2 | +20.2% |
| same body as a native Luau loop | 8.4 ns/iteration | | |

With `luau --codegen` (native codegen): 80.6 / 116.2 / 81.7 / 164.0 / 65.5 ns/op with the counter, i.e. roughly 2x faster. Counter overhead is noisy between runs (-6% to +35%); treat it as "about 10%". Context: a dialogue step runs tens to hundreds of ops, so 50 to 100 microseconds at worst; the stock-VM cost matters only for timer loops and `freeloop` jobs. The VM's own instruction budget (interrupt callback, which exists in the C API but not the CLI) would replace this counter. Note my budget unit differs from rAthena's: rAthena counts VM stack tokens (65535 per `run_script_main` slice, 2048 jumps), I count flat ops (10,000 per yield slice, 2048 jumps incl. callsub); that is a policy decision, not a measurement.

## 5. Verdict: GO-WITH-CHANGES

The approach works. The legacy dialect parses completely (0 failures over 13,850 bodies in both rulesets, 3 s total), every body lowers, a coroutine host with heap frames runs all of them in the scripted client without interpreter crashes, and the properties the architecture relied on (locals/labels, goto/callsub/switch as pc assignments, blocking commands as yields, typed per-NPC unsupported errors) hold in running code. Changes before committing:

1. Make expression evaluation non-recursive for conditional calls (stack-machine or jump-lowering of `?:`/`&&`/`||`) or accept and document the 4,000-deep host-recursion limit and the 111 sites. Add reference-argument semantics (`argrefs`, `getarg`/`getvariableofnpc`/`getd` as lvalues) to the design text; the spec does not mention it and the corpus uses it.
2. Replicate or fix the parser quirks deliberately: `if (a) && (b)`, blind terminator skip (3 sites), arity as hard errors, case-insensitive identifiers (labels, commands, variables all fold), `case` operands restricted to constants.
3. Replace the eager AST (107 to 136 B/token, 250 to 300 MB for renewal) with the compact cached form the architecture already proposes and load per NPC on first use; duplicates already share one body (10,498 + 5,427 clones). The compact encoding was not built or measured here.
4. Treat the command library as the real work: 401 distinct names are called, my base table covers 135 and leaves 16.3% of bodies with at least one unsupported call; the long tail is mostly world/battleground/unit commands.

### Three biggest risks

1. **Semantic fidelity is unproven.** 100% parse acceptance is not equivalence. No legacy VM was available, so nothing was compared against golden traces. Known deviations that would show up in a record/replay diff: hoisted calls evaluate before the rest of their expression (`x += f()` reads `x` after the call), `==` on mixed types returns false instead of raising, integers are doubles with `bit32` ops (no int64/overflow rules), `rand` is a stub LCG, `mes`/menu string escapes are simplified, `select` with zero visible options ends the script. The architecture's record/replay harness (section 8c) is a precondition, not an option.
2. **World-state dependence limits what any scripted client proves.** Menu-only DFS reaches 25% of ops corpus-wide; 66% with the branch oracle, and the oracle drives values inconsistent with the state (so some oracle-run errors are artifacts). Constants are another hidden dependency: `consts.luau` has 40k names, but only literal values and item AegisName ids are real; enum-valued constants got synthetic ids, which is wrong for any script that compares them to real data (`Class`, `EQI_*`, skill ids). A real conformance run needs the item/mob/skill DBs and real constant values.
3. **Memory and long-tail commands.** Eager parse costs 250 to 300 MB for the renewal set and about 150 ns/op unoptimized; both are acceptable for dialogue but not free, and 16% of bodies still hit an unsupported command with the base table. Events (`OnTimer`, `OnClock`, touch, mob death), `sleep` wake-up scheduling, `attachrid`, player-queue semantics, `getvariableofnpc` across duplicated NPCs, and `OnInit` ordering were not exercised at all: the driver only enters each `On*` label once as a plain call.

### What I could not verify

- Parity with rAthena's own parser and VM (no built map-server; no golden traces).
- Whether `.` variables are per NPC instance for `duplicate` clones (modelled per NPC; the spec marks it as Q-H).
- Compact binary AST cache size and load time (not built).
- Persistence, account/global variable limits (33-char key, 255-char value), multibyte (GBK) escape handling in strings (`p[-1] <= 0x7e` rule), `npc_parsesrcfile` map/coordinate validation.
- Numeric values of enum-backed constants and of mob/skill name constants (not in the extracted set).
- Native-codegen numbers are from the CLI's `--codegen` flag on this machine only.

## Replay-driven corrections (this supersedes the findings it names)

Source: `../replay/` (real legacy script VM vs this interpreter, 2,485 runs, 53,565 compared host calls; numbers in `../replay/README.md`).

* **Finding 1 above (111 `callsub`/`callfunc` under `?:` `&&` `||` need host-stack recursion) no longer holds**: the legacy VM does not short-circuit
  (`C_LAND`, `C_LOR`, `C_OP3` are stack operators), so all operands run and the calls are hoisted like any other (1,082 renewal + 344 pre-renewal sites, 0 left in
  expressions). The evaluator has no recursion path for them; the `HOST_STACK_OVERFLOW` limit measured in M0(b) cannot occur for script calls. `t_semantics.luau` now
  runs the same chain to depth 20,000 on heap frames.
* **`switch`**: statements between `switch (...) {` and the first `case` run; the compare-chain image (default flag `lower.switchCompat = true`) leaves `$@__swN_val`
  like the legacy compiler (N counts if/while/do/for/function/switch and every `case`). With the flag off the table dispatch of the original design is used. The
  `switch dispatch` micro-benchmark of M0(b) measured the table form; the compat form costs about 3 more ops per `case` tested.
* Other semantic bugs found and fixed (all pinned by `../replay/corpus/synthetic.txt` and `t_semantics.luau`): string results of hoisted calls were coerced to 0; commands to the
  left of a hoisted call ran after it; the index expression of a variable passed to `callsub` was evaluated twice; `getarg` returned the value at call time instead of reading the
  reference; a negative array subscript did not end the script; `& | ^ << >> ~` were 32-bit; instance variables were writable outside instances; a failing `rand()` did not end
  the script; `input` clamped/returned the wrong way; `atoi`, `strcmp`, `strnpcinfo`, `explode`, `implode`, `replacestr`, `countstr`, `substr`, `strpos`, `charislower/upper`,
  `sprintf` without arguments, `getitem/delitem/countitem` failures, `mobcount` on unknown maps, `warp` / `getmapxy` / player position, `checkre`... differed from the legacy command.
* Invented constants removed: the stubs for `getitemname` ("Item#id"), `getequipid`/`getequipisequiped` ("rich" profile), `getguildname` ("Guild"), `getpartyname`, `getguildmaster`,
  `getmapusers`... are replaced by the legacy behaviour for an empty world or by the data-provider interface (`world.luau`), see `README.md`.
* New commands: `getpartnerid sprintf sscanf strcmp strtolower strtoupper strtol axtoi charisalpha/lower/upper insertchar delchar setchar inarray countinarray distance isday isnight
  getlook nude getitem2/3/4 getbattleflag getiteminfo getmonsterinfo mesitemlink mesitemicon getstatus getgmlevel mercenary_get/set_faith getgdskilllv is_guild_leader isdead` and the
  no-op client commands listed in `commands_ext.luau`.
* Parse/lower of all 13,850 bodies is unchanged (0 failures, `run_parse.luau -a re|pre lower`); lowered ops 662,653 (renewal) and 344,559 (pre-renewal); the per-op cost is in the noise of M0(b)
  (`run_bench.luau`: 180 to 280 ns/op with the counter, same machine class).
