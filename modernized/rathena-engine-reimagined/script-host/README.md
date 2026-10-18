# script-host (rathena-engine-reimagined)

Embedded Luau host: one sandboxed `lua_State` per mod, an ordered cancellable
event bus with transactional effect queues, coroutine dialogue, step/time
budgets, memory caps, generational hot reload, a namespaced storage API, a
typed API schema and numeric helpers. Stack: C++20, CMake, Luau 0.690.

Status: implemented against its acceptance tests (see "What is and is not done").
Single-threaded by design: one game thread drives every call.

- `include/script_host/` public API and domain model
- `src/` implementation (`sandbox.cpp` is the only file that touches the Luau C API)
- `api/engine.yaml` schema; `api/engine.d.luau` is **generated from it and checked in**
- `tools/gen_api.cpp` the generator (C++, uses the same reader as the tests)
- `tests/` acceptance tests; `PENDING(rule, name)` is an expected failure
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

**Numeric** (`numeric.hpp`). `NumCtx` int64 methods: `add sub mul mul_pct idiv
f32_step min1 set`; overflow throws, `idiv` truncates toward zero, `mul_pct`
uses a 128-bit intermediate (GCC/Clang). `RngStreams`: main stream is
`std::mt19937`; each hook gets a SplitMix64 stream derived from
(seed, mod, hook id). `strict_parity` makes `main_next_from_hook` (Luau
`rng.main`) throw and makes load reject `draws_main_rng` hooks; `ReloadManager`
copies the flag from its `RngStreams` when validating.

## What is and is not done

Implemented and tested: everything above. Still expected-failure:
`definition shape change reports needs_restart` keeps its original source
(`-- changes a fields schema`, a comment), which carries no shape information;
the real mechanism (`definitions.declare`) has its own passing test.

Not implemented (needs other units or was out of scope): sockets and the real
database; per-tick script cap degradation (skipping observe hooks first);
per-hook timing / slow-hook log; the operator kill switch at mod level; the
`import`/exports cross-mod call path; lazy read-through entity handles
(`EntityHandle` is only the type); `unload`; Windows/MSVC (needs `__int128`).

`api/engine_globals.d.luau` (`import`, `engine`) is the older hand-written
declaration of host globals the mods unit uses; it is not generated.
