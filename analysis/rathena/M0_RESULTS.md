# M0 Results: measurement gate for the engine + Luau architecture

**Date:** 2026-10-18. **Luau:** tag 0.690, built from source (`git clone --depth 1 --branch 0.690`), Release, 4-core x86-64 container, stock VM (no `--codegen`) unless stated.
**Code:** `modernized/rathena-engine-reimagined/tooling/m0/` (host, scripts, `run_m0.sh`) and `.../m0/legacy_interp/` (M0b). Raw M0b outputs are in `legacy_interp/out/`.
**Verdict summary:** all four gates pass. Two design forks resolve in favor of the simpler option, and several risks the critic raised are confirmed, corrected or newly found.

| Gate | Question | Verdict |
|---|---|---|
| M0(a) | Is pure Luau damage fast enough, so `native-defaults` damage stages can be skipped? | **GO: skip native damage stages for now** |
| M0(b) | Does a Luau-hosted AST interpreter run the legacy NPC corpus? | **GO-WITH-CHANGES** (fidelity still unproven) |
| M0(c) | Do hooks, sandbox, memory accounting and budgets work at scale? | **GO** with two required changes |
| M0(d) | Do `ctx` int64/f32 methods reproduce the worked examples? | **GO** with one lint rule |

## M0(a): damage cost

`m0a_damage.luau`, `m0a_heavy.luau`. Same inputs, identical checksums across all three implementations.

| Implementation | µs/hit | hits/s |
|---|---|---|
| A: pure Luau, double math, 9 stages | 0.44 (0.50 with interrupt callback) | 2.3 M (2.0 M) |
| B: Luau stages driving native int64 `ctx` | 0.74 (1.00) | 1.3 M (1.0 M) |
| C: whole pipeline as one native call | 0.09 | 10.8 M |
| Heavy hit: 13 stages, ~40 native reads, 52 status checks, 3 hooks | 3.3 to 3.9 (interrupt on / off, noisy) | 250 to 310 k |

An AoE burst of 50 targets costs about 0.2 ms in the heavy case, so about 25 such bursts fit in a 5 ms script budget (25% of a 20 ms tick).

**Decision:** the damage pipeline ships in pure Luau with `ctx` int64 methods (B). The `native-defaults` **damage stages stay unimplemented** (the scaffold keeps their stubs and the opt-in registry so they can be added if a later measurement demands it). **Mob AI remains the only required native default.** This resolves HITL #2 item 2 in the user's favor: the generic core and the first milestone contain no formulas at all outside the rulesets.

Caveats: the pipelines are synthetic (the real one has more branching), the numbers are from one machine, and A beating B means native-call overhead per `ctx` method exceeds the cost of double arithmetic. Re-measure with the real PRE and RE pipelines once they are implemented; the heavy case leaves roughly a 10x margin.

## M0(b): legacy NPC interpreter

Full report: `modernized/rathena-engine-reimagined/tooling/m0/legacy_interp/RESULTS.md`. Parser, lowering to a flat op list with jump targets, coroutine-hosted interpreter with heap frames.

- **Parse success: 0 failures over 13,850 script bodies** (renewal 9,368 of 835 files; pre-renewal 4,482 of 527 files). Parse about 2.1 s and 1.0 s; lowering 0.7 s and 0.3 s. `duplicate` clones share one body.
- **Memory is the cost:** eager AST about 250 MB (renewal) and flat ops about 300 MB. A compact or lazy form is required (see changes).
- **Five worst-case files** (686 to 18,841 lines) all parse and run; branch-flip coverage 71 to 95%; no interpreter bugs; whole-corpus drive of 13,605 bodies found **0 interpreter bugs**. `agit_main_se.txt` with `freeloop` ran without runaway.
- **Interpretation cost:** 155 to 290 ns per op (65 to 165 ns with `--codegen`). A per-op step counter for budgets costs about 10% (noisy).
- **Corrections to the architecture:** `goto` appears at only 107 call sites; the labels-inside-blocks (25), call-by-reference through `getarg`/`getvariableofnpc` and conditional calls inside `?:`/`&&`/`||` (111, host-stack recursion, fails at about 5,000 depth) were not in the design.
- **Not proven:** equivalence to rAthena. Nothing was compared with the legacy VM; menu-only exploration reaches about 25% of ops corpus-wide and constants are synthetic. 16.3% of bodies use a command the base table lacks (0.91% of call sites).

**Required changes (adopted in architecture v2.1):** (1) record/replay against the legacy VM is a precondition for calling the compatibility layer done; (2) parse lazily per file or NPC and keep flat ops (not the AST) with shared duplicate bodies; (3) add call-by-reference to the interpreter design; (4) cap host-stack recursion with a typed `HOST_STACK_OVERFLOW` and a documented limit (about 4,000); (5) the command tail (about 270 unimplemented called names, events, timers, `sleep`, `attachrid`) is the real remaining work.

## M0(c): hooks, sandbox, memory, budgets

- **Bus dispatch** (3 hooks, priority order, cancellable): 0.18 µs/event (0.21 with interrupt). 1000 CCU all walking (13,400 events/s) costs **2.4 to 2.8 ms of script per second, about 0.05 ms per 20 ms tick**. Hooks on every move are affordable.
- **Sandbox** (`luaL_sandbox` + sandboxed thread): writes to shared globals, string metatable and `math` are rejected as read-only; `os.execute`, `io`, `debug.getinfo` and `loadstring` are absent from the host's `luaL_openlibs`; the 1 GB `string.rep` bomb fails with "not enough memory" under a 64 MB allocator cap. **`getfenv` is present and must be removed from mod environments.**
- **Budgets:** the interrupt callback stops `while true` at the 0.5 s deadline, and also stops pathological `string.find` backtracking and a comparator-based `table.sort`. **Correction to the critic's claim:** `string.find` backtracking *is* interruptible in 0.690. **Confirmed:** `table.sort` of 4 M numbers with the *default comparator* ran 0.543 s against a 0.2 s budget (not interruptible), and `table.concat`/`string.rep`/`gsub` over large inputs also finish past tight budgets but are bounded by the memory cap.
- **Per-mod accounting:** a single Luau allocator callback cannot attribute memory to a mod. **One `lua_State` per mod works:** 236 KB and 0.064 ms per sandboxed state (200 states: 46 MB, 12.7 ms), and a memory bomb in one state hit its cap while a neighbouring state kept working.
- **Interrupt overhead** is about 0 to 35% depending on the code (noisy); acceptable.

**Required changes:** (1) one `lua_State` per mod is the default, with cross-mod calls marshalled through C++ (cost of cross-state calls not yet measured); (2) mod environments drop `getfenv` and replace `table.sort` with a bounded version (size cap, always uses the interruptible comparator path).

## M0(d): numeric fidelity

`m0d_numeric.luau`: **17 of 17 worked-example values reproduce exactly** (E-1…E-9 including the PRE/RE branches, E-4b, and a float32 crit step). Native `ctx` keeps int64 values past 2^53 where Luau doubles lose precision. **Hazard found:** Luau `//` floors negatives (`-7 // 2 = -4`) while legacy C++ truncates (`-3`), so ruleset code must use `ctx:idiv` / `num.idiv` and a lint rule must flag `//` and `/` on signed quantities in ruleset packages.

Caveat: several example checks are arithmetic on the spec's own formulas, so this validates the method semantics, not bit-exactness against legacy golden traces. Golden-trace conformance remains the real test (BR-RULESET-119).

## Tooling findings from running the real analyzer

- 33 scaffolded files used a directive that Luau does not know (`--!strict-compatible`); now `--!nonstrict`.
- `luau-analyze` (0.690 CLI) has no option to load host global declarations, so the injected `import` global is still reported unknown in `ruleset-prerenewal`; solve by running the analyzer through the host's `Luau::Frontend` or a pinned CLI with `--defs`. The `native-defaults` Luau references need type annotations.

## What M0 did not cover

- Real PRE/RE pipelines and golden traces; cross-state call cost between mod states; `lua_State` snapshotting for reload; multi-client packet handling; PostgreSQL write-behind under load; the compact on-disk AST cache; legacy-VM record/replay.
