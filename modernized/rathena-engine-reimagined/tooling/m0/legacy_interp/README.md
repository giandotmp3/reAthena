# M0(b) spike: legacy rAthena NPC dialect on a Luau parser + flat-op interpreter

Everything here is plain Luau (parser, lowering, interpreter, commands, driver). Python (`prep.py`) is glue only: it walks
the `npc/*/scripts_main.conf` import graph, wraps each `.txt` as a Luau data module (the Luau CLI has no `io`), and extracts
the builtin argument specs (`BUILDIN_DEF`) and script constants from `src/map/`.

Results and verdict: `RESULTS.md`. Raw outputs: `out/*.txt`.

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
- `commands.luau`   step 3b: base command table (requested ~40 + recording stubs for the next ~60 by usage). `commands_ext.luau`: the on-demand layer (string functions and more stubs). Unknown commands are absent from both tables, so the interpreter raises `{type="UNSUPPORTED", cmd=...}`.
- `driver.luau`     step 4: scripted client. DFS over menu/select/prompt answers by replay, plus an optional branch-flip oracle (every conditional branch/switch becomes a choice point).
- `npcrun.luau`, `run_*.luau`, `t_*.luau`, `world.luau`, `corpus.luau`: harness and measurements.
