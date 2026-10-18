# script-host (rathena-engine-reimagined)

Embedded Luau host: one sandboxed `lua_State` per mod, an ordered cancellable
event bus with transactional effect queues, **named stage pipelines that rulesets
define and mods edit**, coroutine dialogue, step/time budgets, memory caps,
generational hot reload, a namespaced storage API, a typed API schema and numeric
helpers (`num.f32`). Stack: C++20, CMake, Luau 0.690.

Status: implemented against its acceptance tests (see "What is and is not done").
Single-threaded by design: one game thread drives every call.

- `include/script_host/` public API and domain model
- `src/` implementation (`sandbox.cpp` is the only file that touches the Luau C API)
- `api/engine.yaml` schema; `api/engine.d.luau` is **generated from it and checked in**
- `tools/gen_api.cpp` the generator (C++, uses the same reader as the tests)
- `tests/` acceptance tests (`test_pipeline.cpp`: the pipeline runtime; `test_rulesets.cpp`: the two ruleset packages
  running on it); `PENDING(rule, name)` is an expected failure
  tagged with the rule ID, and an unexpected pass (XPASS) fails the run;
  `LUAU_TEST` / `LUAU_PENDING` report `SKIP` when the build has no Luau VM
- `luau/sandbox_probes/` hostile mod snippets (all are run by the sandbox suite)
- `luau/typecheck/` mod snippets type-checked against `engine.d.luau`

## Build and run

```sh
export RATHENA_LUAU_ROOT=/path/to/luau/src          # dir with VM/include and Compiler/include
export RATHENA_LUAU_LIB_DIR=/path/to/luau/build     # optional: dir with libLuau.*.a
cmake -S . -B build && cmake --build build && ctest --test-dir build
build/tests/script_host_tests --tag BR-RULESET-116   # one rule tag
```

Luau discovery (no network access is ever attempted):

| Variable | Meaning |
|---|---|
| `LUAU_ROOT` (cache; default `$RATHENA_LUAU_ROOT`) | Luau source root containing `VM/include/lua.h` and `Compiler/include/luacode.h` |
| `LUAU_LIB_DIR` (cache; default `$RATHENA_LUAU_LIB_DIR`) | where `libLuau.VM.a`, `libLuau.Compiler.a`, `libLuau.Ast.a` live; if empty, `../b`, `../build`, `./build`, `./cmake`, `./lib` and `.` next to `LUAU_ROOT` are searched |
| `SCRIPT_HOST_WITH_LUAU` | `AUTO` (default: link if found), `ON` (fail if missing), `OFF` |

If Luau is not found the build still succeeds: the VM-dependent classes compile
to stubs that report an error, and every `LUAU_TEST` prints `SKIP` with the
reason (the run still exits 0). If `libLuau.Analysis/Config/EqSat` are also
present, `decl_check` is built and the `engine.d.luau` type-check test runs
(`ctest` shows it as `engine_d_luau_typecheck`); otherwise that test is skipped.

Regenerate the declarations after editing `api/engine.yaml`:

```sh
cmake --build build --target generate_api      # rewrites api/engine.d.luau
build/gen_api api/engine.yaml --luau api/engine.d.luau --check   # CI freshness check
```

A test also fails if the checked-in file differs from the generated output.

## Design in one page

**ModSandbox / ModState** (`sandbox.hpp`; `ModState` is an alias). One
`lua_State` per mod with its own limiting allocator (cap from
`ModEnvironmentSpec::memory_cap_bytes`, accounted in `MemoryAccountant`).
Libraries opened: base, coroutine, table, string, math, bit32, utf8, buffer,
vector. Not opened or removed: `os`, `io`, `debug`, `require`, `loadstring`,
`getfenv`, `setfenv`, `print` (re-routed to a host log callback),
`collectgarbage` (only `"count"`). `table.sort` is replaced by a version that
refuses tables over `max_sort_elements` and always sorts through a Luau
comparator so every comparison is an interrupt safepoint. `string.rep` is
replaced to fix the stock int cast (a 2^31 repeat count wraps to "") and to
refuse results that cannot fit the remaining memory. `luaL_sandbox` then makes
every library, the string metatable and the globals read-only; each mod runs in
a `luaL_sandboxthread` proxy global table, so its own globals are private.

**Budgets.** The interrupt callback counts safepoints (calls and loop
back-edges, not instructions) against `steps_per_dispatch` (hooks, timers,
load), `steps_per_slice` (dialogue resumes), and samples the wall clock every
256 steps against `wall_us_per_dispatch` and `wall_us_per_tick`. A trip is a
typed `RunStatus::BudgetExceeded` (`HookFailure::Kind::Budget` for hooks). The
error is re-raised at every later safepoint until the host re-arms, so `pcall`
inside the script cannot swallow it. Only the offending script aborts; the
sandbox stays usable. Three budget strikes in `strike_window_ticks` auto-disable
the hook. `Permission::UnboundedLoop` lifts the step limit only; the wall clock
then uses `wall_us_per_tick`. Any failed allocation during a run (even if the
script catches it) disables the mod until `reenable()` (which also collects
garbage, because Luau does not collect on allocation failure).

**EventBus** (`event_bus.hpp`). Names are `namespace:event`. Order: phase
(pre, on, post), then `priority` (lower first), then `before`/`after`
constraints (topological, within equal phase+priority), then mod load order
(`set_mod_load_order`, else first registration), then registration sequence.
Load-time `LoadError`s name both hooks for: a constraint that contradicts
(phase, priority) order, constraint cycles, duplicate hook ids, two `replace`
hooks on one (event, phase) unless exactly one declares `precedence`, `replace`
on a protected stage without `permit_protected`, and `draws_main_rng` under
`strict_parity`. Constraints naming an unregistered hook are ignored. `cancel()`
in a `pre` hook rolls back the effects queued during that dispatch (effects the
caller queued earlier survive) and skips every later hook; elsewhere it is
ignored with a diagnostic. A failing hook is rolled back (its effects and
`ctx` changes), recorded in `DispatchResult::errors`, and the dispatch goes on.
`observe` hooks may enqueue effects but cannot change `ctx` (enforced in Luau
and again natively). Only the winning `replace` hook runs; `replaced_by` tells
the caller to skip its default stage.

**Reload** (`reload.hpp`). `reload(mod, source)` builds generation N+1 in a
fresh sandbox, validates a copy of the bus (old generation removed, new
hooks added), then commits atomically; any failure leaves generation N active.
`definitions.declare(id, {fields})` records the definition shape; a change at
reload is refused with `needs_restart` and the exact difference. Timers
(`timers.after`, `ReloadManager::schedule`) are owned by (mod, generation) and
cancelled on reload. Dialogues hold a lease on their generation: they finish in
the old generation until `drain_timeout_ms` of game time passes, then are
cancelled. `advance_game_time(ms)` takes a delta; `DialogueManager::advance_time(now_ms)`
takes absolute time.

**Pipelines** (`pipeline.hpp`, `PipelineRegistry`, architecture 6.4). A ruleset declares a pipeline with
`pipeline.define(name, {stages})` and registers the BODY of each stage with `pipeline.stage(name, stage, fn, {protected})`;
it runs the pipeline with `pipeline.run(name, ctx, opts)` instead of a private loop. Other mods edit the effective order with
`insert_before / insert_after(name, anchor, id, fn, opts)`, `replace(name, stage, id, fn, opts)`, `cancel(name, stage)` and
`move(name, stage, {before=|after=}, opts)`. Define, stage and every edit are load-time calls; `run`, `describe(name)` (the
effective ordered list, cancelled stages included as `active = false`, with the source mod of every edit) and `order(name)` work
any time. Rules, all load-time `LoadError`s naming the mods:
two `replace` (or two `move`) of one stage are an error unless exactly one sets `precedence` (the other is recorded in `describe`
as skipped); `replace` + `cancel` of one stage is an error; replacing, cancelling or moving a stage the ruleset registered
`protected` needs `Permission::ProtectedStages` in the editing mod's environment; inserted ids are `<mod id>:<name>` and unique;
an edit naming a missing pipeline or stage is refused unless `optional = true`; stages on one anchor run by `priority`
(lower first), mod load order, registration; `draws_main_rng` under `strict_parity` is refused. The effective order is computed from
the definition plus the current edits and never written back: removing a mod or a generation removes exactly its edits, and edits
to one pipeline cannot touch another. A reload builds a candidate registry (old generation removed, new ops applied, every
pipeline re-planned) next to the candidate bus and commits both together, so a conflict, or a stage another mod's edit still
anchors on, refuses the whole reload and leaves generation N running.
A stage body in the defining mod's own state is called with the `ctx` given to `run`. A body in another mod's state runs as a
hook: its `HookContext` carries the run's "lane" (the int64 the ruleset exposes as `opts.lane`, an object with `get`/`set`; read
before and written back after a modify/replace stage), an optional read-only snapshot (`opts.view`, copied lazily, read with
`ctx:view()`), the run's transactional effect queue and its own RNG stream. A failing hook is rolled back (lane, effects) and
recorded in the result; budget strikes disable it. `ctx:cancel(reason)` aborts the run and discards the run's effects; it is
refused with a diagnostic when it would skip a protected stage and the hook's mod has no permission. `opts.skip` is a set of
stage ids not to run this time (stages that only apply to a connected hit). `run` returns nil unless another mod's stage ran or
something is worth reporting. Only the defining mod's current generation may run its pipeline. Observe stages cannot change the lane.

**Numeric** (`numeric.hpp`). `NumCtx` int64 methods: `add sub mul mul_pct idiv
f32_step min1 set`; overflow throws, `idiv` truncates toward zero, `mul_pct`
uses a 128-bit intermediate (GCC/Clang). `f32_round(x)` / Luau `num.f32(x)` is `(double)(float)x` (ties to even,
denormals exact, past the binary32 range +-infinity, NaN and infinities pass through); float32 `+ - * /` are `num.f32(a op b)`.
`RngStreams`: main stream is
`std::mt19937`; each hook gets a SplitMix64 stream derived from
(seed, mod, hook id). `strict_parity` makes `main_next_from_hook` (Luau
`rng.main`) throw and makes load reject `draws_main_rng` hooks; `ReloadManager`
copies the flag from its `RngStreams` when validating.

**Config** (`ModEnvironmentSpec::config`). The loader's per-mod values become the read-only Luau table `config` (for a ruleset:
its `[ruleset]` table, notably `legacy_quirks`, which defaults to `true`: legacy defects ship as-is unless the manifest says
otherwise). The mods unit still has to copy `manifest.ruleset->legacy_quirks` into it. The globals `pipeline`, `num` and `config`
are always defined; a global `engine` (all namespaces in one table, `type Engine` in `engine.d.luau`) exists only when
`ModEnvironmentSpec::expose_engine` is set, because the mods bundler calls `register(engine)` on every entry module when it exists.

**Dialogue** (`dialogue.hpp`). `say`, `choose`, `input`, `wait` yield to the host
(valid only directly inside the dialogue coroutine). Resume tokens are
single-use 128-bit values; a stale, forged or other-player token, a reply of the
wrong kind, or a `choose` index outside 1..n is rejected without consuming the
dialogue. `wait` resumes only from `advance_time`. Disconnect cancels; idle
dialogues time out (`idle_timeout_ms`, reported by `advance_time`).

**Storage** (`storage.hpp`). `storage.character(id)`, `storage.account(id)`
(needs `Permission::StorageAccount`), `storage.world()`; `:mod("other")` needs
`Permission::StorageReadOther` **and** a host `grant_read`. Values: int64,
double, string, bool, nested tables with string keys and integer keys, depth
limit 16. Per-mod quota (default 1 MiB), versioned rows,
`storage.migrate(current_version, fn)` where `fn(from, value)` returns
`new_version, new_value`; steps chain up to the current version and the migrated
row is written back. `check_loadable` probes the chain with an empty row. The
backend is the `StorageBackend` interface with an in-memory implementation; the
persistence unit plugs in a durable one. Lua numbers are doubles, so integers
beyond 2^53 are exact only on the C++ side.

## What is and is not done

Implemented and tested: everything above. Still expected-failure:
`definition shape change reports needs_restart` keeps its original source
(`-- changes a fields schema`, a comment), which carries no shape information;
the real mechanism (`definitions.declare`) has its own passing test.

Pipelines are tested at the registry level (no Luau), through ReloadManager with real mod states, and against the two ruleset
packages: `test_rulesets.cpp` replays both golden suites (5,424 and 8,858 cases) through `pipeline.run`, applies the 23 operations of
`ruleset-renewal/docs/PIPELINE_DIFF.md` (and the 21 that `move` allows) to the pre-renewal order, and checks the Luau fake host the
packages' CLI suites use against the real runtime. Not done in the pipeline work: a hook in another state sees the lane and a copied
`view`, not live entity handles; the mods unit does not yet fill `ModEnvironmentSpec::config` from the manifest or grant
`ProtectedStages` (no permission kind for it in the manifest vocabulary); `use_native` (opt-in native stages) is not implemented;
`pipeline.run` binds results as values (no streaming of per-stage timings).

Not implemented (needs other units or was out of scope): sockets and the real
database; per-tick script cap degradation (skipping observe hooks first);
per-hook timing / slow-hook log; the operator kill switch at mod level; the
`import`/exports cross-mod call path; lazy read-through entity handles
(`EntityHandle` is only the type); `unload`; Windows/MSVC (needs `__int128`).

`api/engine_globals.d.luau` (`import`, `engine`) is the older hand-written
declaration of host globals the mods unit uses; it is not generated.
