# M0(b) spike: legacy rAthena NPC dialect on a Luau parser + flat-op interpreter

Everything here is plain Luau (parser, lowering, interpreter, commands, driver). Python (`prep.py`) is glue only: it walks
the `npc/*/scripts_main.conf` import graph, wraps each `.txt` as a Luau data module (the Luau CLI has no `io`), and extracts
the builtin argument specs (`BUILDIN_DEF`) and script constants from `src/map/`.

Results and verdict: `RESULTS.md` (original M0(b) measurements + the "Replay-driven corrections" section at the end). Conformance against the real
legacy script VM: `../replay/` (`README.md`, `results/`). Raw outputs: `out/*.txt`.

## Rerun

```sh
cd /tmp/claude-0/-home-user-reAthena/1d67890b-8b04-5f37-9297-104bbabb4a76/scratchpad/m0b
LUAU=../luau-src/b/luau ./run_all.sh        # ~4 minutes; writes out/*.txt
```

Individual pieces (CLI args go after `-a`):

| What | Command |
|---|---|
| Regenerate data modules (needs repo at /home/user/reAthena) | `python3 -I prep.py` |
| Declaration splitter over both load sets | `luau t_split.luau` |
| Parse (and lower) every script body of a load set | `luau run_parse.luau -a re\|pre [lower]` |
| Command usage histogram + static command-table coverage | `luau run_usage.luau` |
| goto/callsub/label density ranking | `luau run_density.luau` |
| Unrepresentable / stressful constructs, file:line | `luau run_constructs.luau` |
| Semantic regression tests (locals/labels, switch, callsub, refs, menus, depth) | `luau t_semantics.luau` |
| Parser negative tests | `luau t_neg.luau` |
| Drive one file: `<npc path> <re\|pre> <runBudget> <base\|ext> <flipBudget>` | `luau run_worst.luau -a npc/cities/prontera.txt re 200 ext 1500` |
| Drive the whole corpus (crash hunt, unsupported inventory) | `luau run_corpus_drive.luau -a re\|pre base\|ext` |
| ns per op benchmark (counter on/off) | `luau [--codegen] run_bench.luau` (after `./mk_nocount.sh`) |

## Files

- `declsplit.luau`  step 1: header splitter (mirrors `npc_parsesrcfile` / `npc_skip_script`).
- `parser.luau`     step 2a: tokenizer + recursive-descent parser to a positional-array AST (rA operator table, `parse_callfunc` rules, `case` / `default`, `for` / `do` / `while`, labels, inline `function`).
- `lower.luau`      step 2b: AST to a flat op list with jump targets (`JZ/JNZ/JMP/SWITCH/GOTO/CALLSUB/CALLFUNC/RETURN/END/MENU/EXPR`), label table, constant/param/label resolution, hoisting of `callsub`/`callfunc` out of expressions.
- `interp.luau`     step 3a: coroutine-hosted interpreter, explicit heap frame stack, sigil-keyed variable stores, typed errors, budgets (ops, gotos), optional branch oracle and coverage.
- `cfmt.luau`       C-library `sprintf` / `sscanf` for the commands of the same name (pure Luau).
- `commands.luau`   step 3b: base command table (requested ~40 + recording stubs for the next ~60 by usage). `commands_ext.luau`: the on-demand layer (string functions and more stubs). Unknown commands are absent from both tables, so the interpreter raises `{type="UNSUPPORTED", cmd=...}`.
- `driver.luau`     step 4: scripted client. DFS over menu/select/prompt answers by replay, plus an optional branch-flip oracle (every conditional branch/switch becomes a choice point).
- `npcrun.luau`, `run_*.luau`, `t_*.luau`, `world.luau`, `corpus.luau`: harness and measurements.

## Semantics that follow the real VM (found by `../replay/`)

* **No short-circuit.** `&&`, `||` and all three operands of `?:` are evaluated (`script.cpp` `C_LAND/C_LOR/C_OP3` are stack operators). Because every
  operand always runs, `lower.luau` hoists every `callsub` / `callfunc` / user-function call out of expressions (in evaluation order, together with the
  command calls to their left), and the evaluator has **no host-stack recursion path** any more (`NESTED_CALL` tripwire only).
* **`switch` is a compare chain.** Statements between `switch (...) {` and the first `case` run; a body falling into a middle `default` continues with
  `$@__SWn_VAL = 0`; `break` jumps to the end label. The legacy compiler leaves the global `$@__swN_val` behind (non-zero when no case matched); `N` is the
  parse-order index shared by `if/while/do/for/function{}/switch` **and every `case`** (its generated `if`), printed in hex.
  Compat flag `require("./lower").switchCompat` (default **true**): `false` selects the faster table dispatch (still runs the pre-case statements,
  leaves no global).
* **Variables passed to `callsub`/`callfunc` are references**: the index expression is evaluated once, `getarg` reads the current value when it runs.
* **Array subscript < 0** ends the script (legacy `getelementofarray`); instance variables (`'x`) read as 0/"" and cannot be written outside an instance.
* **Command failure** (`SCRIPT_CMD_FAILURE`) is reported with `I.cmdFail(st)` (`st.cmdFailed`); hosts record it (the replay trace prints `!F`).
* **Data-provider interface** (`world.luau`, `W.newEnv(funcs, seed, data)`; all optional): `itemname`, `itemid`, `battleflag`, `castle`, `shop`, `mapexists`,
  `lookup(cmd, ...)` (getiteminfo / getmonsterinfo / getitemslots / mesitemlink / getcastlename / jobname / getmapflag / checkcell / getusers). Without a
  provider the commands answer like the legacy VM does for an unknown key; **no item names or other game data are invented**. A host may also supply
  `env.rand(lo, hi)` (the random source).
* Player model additions: `x,y,map`, ids (`charid`, `accountid`, `gameid`, `partyid`, `guildid`), `equip[pos]` (empty), `look`, `status`, `partnerid`.

Tests: `luau t_semantics.luau` (29 checks incl. the ones above), `luau t_neg.luau`.
