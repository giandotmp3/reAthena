# rAthena Engine + Luau Mods: Reimagined Architecture (v2, post-critique, awaiting HITL #2)

> **v2 changes** (full disposition in §14): (1) the "declarative-program executor" is replaced by named native default stages in a separate `native-defaults` module; (2) the legacy NPC runtime is a Luau-hosted AST interpreter, not a compiler; (3) the gateway is a separate thread pool/role behind the session seam, with a failure-mode table; (4) both rulesets stay P0 but are sequenced pre-renewal first; (5) instances, save-format migration, reload generations, mod threat model, ID tombstones and observability are added; (6) **M0 measurement spikes now gate the two design forks.**

**Spec:** `AI_NATIVE_SPEC.md` (base) + `AI_NATIVE_SPEC_ENGINE.md` (delta) + `BR_RULESET.md` (119 ruleset rules)
**Supersedes for this vision:** `REIMAGINED_ARCHITECTURE.md` (in-place C++20 refactor). That document's decision K2 (keep the legacy NPC VM) is **reversed** here: the legacy VM is replaced by Luau plus a compatibility interpreter hosted in Luau. Its `modernized/rathena-reimagined/` scaffold is untouched; reusable parts are listed in §9.
**Status:** awaiting critic review, then HITL #2.

## 1. Principle

> The engine knows *how things move, connect, persist and are scheduled*. It does not know *what anything is*.

Everything with a game name (skill, item, status, job, element, formula, quest, map rule) is a **definition** in a Luau package. The engine sees only opaque namespaced IDs, generic attributes, generic component data, events and hooks.

### 1.1 How the "native fast paths" fit the principle (revised after critique B1)

The vision asks for two things that pull against each other: an engine with no knowledge of formulas, and a native default damage calculation. A closed "op language" executed natively only moves the rules into a second, engine-owned scripting language (rejected in review). The resolution is a **layering**, not a trick:

- **Generic core** (`ecs`, `attrs`, `grid`, `path`, `tick`, `net`, `persist`, `gateway`, `script-host`): contains no game vocabulary and no game numbers. This is the part the "no knowledge of skills/items/statuses/formulas" requirement applies to, and it is lint-checked.
- **`native-defaults`** (separate module, separate library, separately versioned): a small set of **named, versioned native stages**, e.g. `native.damage.weapon_basic@1`, `native.damage.element_table@1`, `native.damage.defense_soft@1`, `native.mob_ai.default@1`. They carry the legacy formulas, parameterized entirely by data tables the ruleset supplies (element matrix, size matrix, curve constants). The engine core never links them; the host loads them as a plugin.
- A **ruleset opts in by ID**: `pipeline.use_native("weapon", "defense", "native.damage.defense_soft@1", { params... })`. A stage with no script hook on it runs the native implementation. If any mod hooks the stage, the stage runs the Luau reference implementation (which the ruleset also ships), so behavior is independent of which path ran.
- **Only the hottest stages get a native version** (target: the five or six that dominate cost). Everything else is Luau only. The differential test (native vs Luau reference) runs on exactly those stages.
- **Mob AI** is a native state machine driven by mode bits and tunables from the mob definition. Scripts supply mob-skill conditions and events. The core has no mob-specific rule.
- **Gate:** M0(b) measures pure-Luau damage per AoE burst with the budget interrupt on. If Luau meets the per-tick budget, damage ships with **no** native stage and only mob AI stays native.

Honest statement for the vision: "the engine core has no game rules; the native default damage calculation is a plug-in library the ruleset opts into". If you want the plug-in library to be forbidden from containing formulas at all, the fast path must be dropped (see HITL #2 question 2).

## 2. C4 container view

```mermaid
C4Container
  title rathena-engine: one deployable, modular inside
  Person(player, "Player client", "Any supported (date, flavour) client version")
  Person(op, "Operator / modder", "Edits mods/, config, runs sim")
  System_Ext(db, "PostgreSQL 16+ / SQLite", "Accounts, characters, mod KV, world state")
  System_Ext(http, "Web clients / launcher", "Emblem, config, party booking")

  System_Boundary(srv, "rathena-engine process") {
    Container(net, "net + netproto", "C++20, generated codecs", "Sockets (epoll/IOCP), per-session codec set, framing, obfuscation, phase gating")
    Container(sess, "gateway", "C++20", "Login, char-select, tickets, account/char lifecycle plumbing; rule-free")
    Container(sim, "sim core", "C++20", "ECS store, map grid, spatial queries, A*, tick loop, timers, instances")
    Container(host, "script host", "C++20 + Luau", "VM, event bus, coroutines, budgets, sandbox, hot reload, storage API")
    Container(persist, "persistence", "C++20", "Write-behind store, KV, record collections, migrations")
    Container(mods, "mods/ (Luau packages)", "Luau + data", "ruleset.renewal / ruleset.prerenewal, content, legacy npc, user mods")
    Container(legacy, "legacy-npc", "Parser + Luau-hosted interpreter", ".txt NPC scripts -> cached AST; 671-command shim")
  }
  Container_Ext(tools, "Tooling", "rathena-sim, rathena-migrate, rathena-pack, codegen", "Headless harness, YAML migrator, API/type generation")

  Rel(player, net, "TCP")
  Rel(net, sess, "pre-world packets")
  Rel(net, sim, "in-world packets")
  Rel(sim, host, "events, hooks, effect queues")
  Rel(host, mods, "loads, sandboxes")
  Rel(mods, legacy, "depends on")
  Rel(sim, persist, "dirty components")
  Rel(host, persist, "mod KV")
  Rel(persist, db, "SQL")
  Rel(http, host, "HTTP routes declared by mods (permission-gated)")
  Rel(op, tools, "CLI")
  Rel(tools, srv, "links same libraries; no network needed")
```

## 3. Topology decision: one process, three interfaces to split later

**Decision:** replace login + char + map (+ web) with **one `rathena` process** containing a `gateway` module (login and char-select) and the world (`sim`). Web routes become mod-declared HTTP handlers on an optional listener.

Evidence (`AI_NATIVE_SPEC_ENGINE.md` §7):

- Legacy default `MAX_MAP_SERVERS` is 2 and the repo shows no multi-map-process deployment. One map process handled 1,000 CCU at 5%–34% of one core.
- ~280 hand-framed inter-server message types, a raw-struct `memcpy` ABI and a 30-second ticket dance across three ports exist only because the roles are separate processes.
- Login is stateless apart from accounts; with one world process, party/guild/whisper/storage-lock authority is loopback.
- The three-port redirect in client packets can carry the same host and port.

Kept as **seams** (C++ interfaces with an in-process implementation, so a later split is a new implementation, not a rewrite): `SessionDirectory` (who is online where), `CharacterStore`, `WorldBus` (cross-world whisper, broadcast, handoff). Horizontal scale beyond one process is explicitly **out of the first milestone**; the escape hatch is sharding by map group behind `WorldBus`, with the gateway as the only public endpoint.

**Roles (revised after critique H1).** The binary takes `--role=all|gateway|world`. The first milestone ships and tests `all`. The gateway has its **own IO thread pool and queues** and talks to the world only through the seam interfaces, so login floods, hashing and char-select cannot add jitter to the 20 ms simulation tick. A real `gateway`/`world` split is then a loopback-transport implementation of the seams, not a redesign.

Threads: network IO threads decode frames only. **One simulation thread** owns the ECS, the Luau VM and all rules (deterministic, no locks). Persistence runs on worker threads. The sim thread hands them **copy-on-write dirty batches** (components are double-buffered at the tick boundary, so the mass-logout case costs a pointer swap, not a 1,000-character copy on the tick). Argon2id runs on a bounded pool owned by the gateway.

### 3.1 Failure modes

| Failure | Behavior |
|---|---|
| Database down | Sim keeps running. Saves queue to a bounded on-disk WAL. New logins and char-select are refused with a clear code. At WAL limit, new writes are refused, not dropped silently. |
| Mod script error / budget exceeded | That hook or coroutine aborts with mod id, hook id and trace. Tick continues. Three strikes in a window auto-disable that hook (operator can re-enable). |
| Mod memory cap exceeded | Mod is disabled and its definitions are marked invalid. World continues. |
| Reload fails to load | Previous generation stays active. Error is reported. |
| Sim tick overrun | Slow-hook log with per-mod/per-hook timings. Optional hook degradation: observe-only hooks are skipped first, `modify`/`replace` hooks are never skipped. |
| Gateway overload | Rate limits and queue caps at the gateway. World is unaffected. |
| Native crash | Whole process exits. Supervisor restarts. Players reconnect. Mid-dialogue coroutines are lost (same as legacy). WAL replays. |
| Corrupt mod KV row | Row quarantined, mod gets a typed error on read; other mods unaffected. |

## 4. Native core (no game knowledge)

| Module | Responsibility | Rule-free by |
|---|---|---|
| `ecs` | Generational entity ids, sparse-set component pools, archetype-free queries, deferred structural changes | Components are engine-defined (`Position`, `Facing`, `Mover`, `Session`, `Collider`, `Persisted`) or **script-declared schemas** (typed fields, registered at load) |
| `attrs` | Generic attribute system: interned attribute ids, contribution lists `(source, op, value)`, incremental recompute by dirty attribute | The ruleset names the attributes (`atk`, `def`…) **and supplies the ordered list of aggregation stages per attribute** (e.g. override, add, mul_pct, clamp, or a renewal-specific chain). The engine only runs the ruleset's stage list. Property test: incremental recompute equals from-scratch recompute |
| `grid` | Cell flags (static and dynamic, ids registered by rulesets), 8×8 buckets, area queries with legacy semantics (snapshot-then-call, mob pass order, optional wall-check) | Cell flags are bits; the ruleset gives them meaning |
| `path` | A\* with legacy tie-breaks (Manhattan×10, diagonal 14, no corner cutting), `path_search_long`, knockback cell search | Pure geometry. Limits (`32`, official-walkpath 14) become engine config |
| `tick` | Single-thread loop, min-heap timers, 20 ms floor, late-timer catch-up rules, virtual clock for tests | Timers carry a callback id or Luau function ref |
| `net` / `netproto` | Sockets, session codec set selected by negotiated `(date, flavour)`, stateful obfuscation key, framing and phase gating | Codecs generated from `proto/*.yaml`; handlers are events (`net:CZ_REQUEST_MOVE`) |
| `persist` | Write-behind of dirty `Persisted` components and KV; schema migrations; online/session tables | Stores opaque typed records keyed by `(owner, kind)` |
| `gateway` | Login auth plumbing, account store, ticket (30 s, single use), char list/create/delete flow, PIN plumbing | Name policy, slot count, PIN complexity, ban order are **config + hooks** supplied by `base` mod (BR-LOGIN/CHAR rules live in script or config, validated by the inherited acceptance tests) |
| `instance` | Map clone with owner (party/guild/char), TTL, instance-scoped timers/NPCs/mobs/storage, cleanup when owner leaves | Mechanism only; legacy `'` variables and `instance_*` commands map onto it |
| `native-defaults` (separate plug-in, §1.1) | Named versioned native stages for the hottest damage stages and the default mob AI state machine | Opted into by the ruleset by ID; parameterized by ruleset data; not part of the generic core |
| `movement` | Walk request → path → per-cell step timers → broadcast; fires `unit:before_move` / `unit:moved` hooks | Replaces rules embedded in legacy `map_moveblock` with an on-move hook |

The engine never includes a header named after a skill, status, item, job or map flag. A CI lint scans `engine/` for the identifier lists extracted from `skill.hpp`, `status.hpp`, `itemdb.hpp` and `mmo.hpp` and fails on any match.

## 5. Script host (Luau)

- **VM:** one Luau state; each mod loads into its own **sandboxed environment** (`luaL_sandbox` style, read-only shared globals, no `os`, `io`, `debug`, `require` of arbitrary paths). Cross-mod calls go through declared `exports`.
- **Event bus:** names are `namespace:event`. Hook = `{id, mod, priority, after/before constraints, phase: pre|on|post, mode: observe|modify|replace}`. Dispatch order is by priority, then topological constraints, then load order, then registration sequence (stable). Any `pre` hook may `cancel(reason)`. Payload is a snapshot plus a mutable `ctx` for `modify`. **World mutation inside a hook is an enqueued effect**, applied by native code after the stage settles (BR-RULESET-116), with a re-entrancy guard.
- **Coroutine dialogue:** `say`, `choose`, `input`, `wait` yield to the engine and resume on the client packet or a timer. Per-player dialogue state is a coroutine plus its resume token; a disconnect cancels it. Idle dialogs time out.
- **Budgets and sandbox:** the Luau interrupt callback (fires on calls and loop back-edges, so this is step/wall-clock accounting, not exact instruction counts) enforces a budget per event dispatch, per coroutine slice and per tick. Exceeding a budget aborts that script with a trace, never the tick. Native library calls that run long inside one call (`string.find` with pathological patterns, `table.sort` of huge tables) are not interruptible, so those functions are wrapped or replaced with bounded versions. Memory: a limiting allocator with **per-mod accounting** (allocator tags; a per-mod `lua_State` is the fallback if tagging proves insufficient in M0). A `unbounded_loop` permission is the `freeloop` equivalent.
- **Threat model and tests:** mods are treated as untrusted. The sandbox suite covers metatable escapes (`getmetatable("").__index`), shared-table mutation (all shared globals frozen), memory bombs (`string.rep`), slow-pattern DoS, and storage-quota abuse (quota enforced in the persistence layer, not only the API). `sql.read` does not exist as a general permission; any SQL access is scoped to the mod's own namespace. Operators may require signed mods in `mods.lock` (provenance field, optional).
- **Hot reload (generations):** reload creates generation N+1 at a tick boundary. Only scripts and hooks hot-reload; changes to definition shape or a `fields` schema require a drain or restart (reported precisely). Timers and event registrations are owned by `(mod, generation)` and cancelled or migrated explicitly, so old closures cannot fire twice or leak. In-flight coroutines finish in generation N, bounded by a timeout (default 60 s game time), then are cancelled. If the new generation fails to load, the old one stays active. A live status or item whose definition disappears is parked as `orphaned` (kept in storage, inert) until the definition returns. Coroutines cannot be serialized, so a restart closes every open dialogue, as legacy does. `rathena-sim` includes reload-during-`select` and reload-with-pending-status-tick tests.
- **Ordering and cancellation:** order is `priority`, then topological `before/after` constraints. A conflict between the two, or a cycle, is a load error naming both hooks. Two mods that `replace` the same stage are a load error unless one declares precedence. `cancel()` in a `pre` hook discards the effect queue accumulated so far for that dispatch (transactional) and skips `on`/`post` hooks. Stages marked `protected` (e.g. anti-cheat gating) cannot be cancelled or replaced by mods without an explicit permission. Hook inputs are **lazy read-through handles with a generation check**, not copied snapshots.
- **Observability:** per-mod and per-hook timing, a slow-hook log (mod id, hook id, stack), tick profile dump, per-mod memory and storage usage, and an operator kill switch (`mod disable <id>`).
- **Storage:** `storage.character(ch):mod("id")`, `storage.account(...)`, `storage.world()`. Typed values (int64, double, string, bool, nested table). Namespaced by mod id automatically. Per-mod quota. Write-behind with versioned rows. Cross-mod access needs the `storage.read:<mod>` permission. This replaces the sigil variable system.
- **Typed API:** one schema (`api/*.yaml`) generates both the C++ bindings and `engine.d.luau`; `luau-analyze` checks every mod in CI. Editor autocomplete uses the same definitions.
- **Numeric fidelity:** Luau numbers are doubles (exact to 2^53). Damage values live in the native `ctx` as `int64` fields and are mutated by methods (`ctx:mul_pct(…)`, `ctx:idiv(…)`, `ctx:f32_step(…)`), which avoids boxed integers and batches native calls. Scripts needing wide integers use these methods or `num.i64` handles.
- **RNG:** the default pipelines draw from a seeded stream with legacy draw counts (BR-RULESET-115). Each third-party hook draws from its **own derived stream**, so adding a mod hook does not shift downstream draws. A per-ruleset `strict_parity` flag (default on in conformance runs) rejects hooks that draw from the main stream.
- **Per-tick script cap degradation:** when the cap trips, observe-only hooks are skipped first. `modify` and `replace` hooks are never skipped (that would change outcomes), so the cap surfaces as an overrun alarm instead.
- **Storage upgrades:** KV rows are versioned. A mod declares `storage.migrate(from_version, row)`; the loader refuses to start a mod whose stored version has no migration path.

## 6. Mods and rulesets

### 6.1 Package format

```
mods/<id>/
  mod.toml          # id, version, engine_api, depends, conflicts, load_after/before, permissions, provides
  scripts/*.luau
  data/*.luau       # generated or hand-written definition tables
  types/*.d.luau    # optional exported types
```

`mod.toml` fields: `id`, `version` (semver), `engine_api` (range), `depends = {"base" = "^1"}`, `conflicts`, `provides = ["ruleset"]`, `load_after`, `load_before`, `permissions` (e.g. `storage.account`, `storage.read:other`, `http.route`, `sql.read`, `unbounded_loop`, `atcommand`, `net.raw`). Permissions are granted by the operator in `mods.lock`. A mod using an ungranted API is refused at load with a precise error.

### 6.2 Namespaced IDs and client-visible numbers

Definitions are keyed `mod:kind/name` (`ruleset.renewal:skill/sm_bash`). The client protocol needs integers for skills, items, mobs, statuses. Rules:

- Definitions may declare `client_id` explicitly. Migrated legacy content keeps its original numbers, so existing clients, GRFs and saved data line up.
- Others are allocated from a reserved range per kind and **persisted in an `id_map` table** so numbers are stable across restarts and mod removal. A freed number is **tombstoned forever** and never reassigned.
- **Save data stores the namespaced string ID**, never the number. Numbers exist only on the wire and in `id_map`, so restoring a backup or running with a different mod set cannot silently re-point an inventory.
- Collision is a load error naming both mods. A CI range-registry check covers shared mods.
- **Client-side limits:** numeric skill, status (EFST) and item IDs are keyed in the client's own files. A mod that adds a skill, status icon or item with client-visible presentation needs a matching client patch. `rathena-pack` generates the client-side tables (`skillinfolist`, `itemInfo`, status icon tables) from definitions. Without a patch, a new definition works server-side but shows the client's default presentation.

### 6.3 Ruleset packages

- `ruleset.renewal` and `ruleset.prerenewal` are **separate packages**, mutually exclusive via `conflicts` and `provides = ["ruleset"]`.
- Shared code lives in a third package `rathena.base`. **Rule: base contains mechanism only** (bonus-vocabulary names, table-loading helpers, test helpers, gateway policy hooks), **never game numbers or formulas.** A CI check fails if base references any ruleset-specific name. Content shared between rulesets is decided by the migrator's diff report, not by guesswork, so drift stays visible.
- Package config carries the six legacy sub-flags (`cast`, `drop`, `exp`, `lvdmg`, `aspd`, `stat`) as defaults per package. Only the two shipped default configurations plus a documented supported subset of combinations are tested; other combinations are "unsupported" (Q-C, recommended answer: bundles, with sub-flags as an advanced override).

### 6.4 Damage pipeline

Stages are named: `init`, `base_attack`, `card_attacker`, `skill_ratio`, `res`, `defense`, `post_defense`, `element`, `size_race`, `card_target`, `target_modifiers`, `map_rates`, `final`. Each ruleset registers its own **order**, because renewal and pre-renewal differ in order, not only constants (BR-RULESET-018/019).

```lua
pipeline.define("weapon", { "init","base_attack","skill_ratio","defense","element","size_race","final" })
pipeline.insert_after("weapon", "element", "mymod:fire_brand", function(ctx) ... end, { priority = 50 })
pipeline.replace("weapon", "defense", "mymod:flat_def")
pipeline.cancel("weapon", "size_race")      -- skip stage
```

Hooks receive immutable unit snapshots plus a `ctx` and return deltas plus an effect queue (BR-RULESET-117).

### 6.5 Status effects

```lua
status.define("ruleset.renewal:status/blessing", {
  client_id = 10, icon = 10,
  fields = { level = "int" },                         -- replaces val1..val4 (Q-F)
  contributes = { str = add(function(s) return s.level end),
                  int = add(function(s) return s.level end),
                  hit = add(function(s) return s.level * 2 end) },
  states = {}, flags = { "debuff_clearable" },
  fail = {}, end_on_start = { "ruleset.renewal:status/curse" },
  resist = resistance.none,
  on_tick = nil,
})
```

The native attribute aggregator runs the ruleset's stage list over each attribute's contributions, evaluating `contributes` functions lazily and caching per `(entity, status_version)`. Invalidation covers equipment, buff and ruleset-flag changes, and a property test checks incremental recompute against from-scratch recompute.

### 6.6 Skills and items

Skills are definitions with `cast`, `requires`, `target`, `hooks = { on_cast_end, calculate_ratio, additional_effect, … }`, covering the nine legacy `SkillImpl` hook points. Items define `equip_bonus` (compiled bonus deltas, replacing `bonus`/`bonus2` script calls), `on_use`, `on_equip`, combos. The ~506 `SP_*` bonus vocabulary becomes the `bonus.*` function table in `rathena.base`.

## 7. Legacy NPC compatibility (`legacy-npc`)

**Choice (revised after critique H2): parse `.txt` to a compact cached AST and run it with an interpreter written in Luau, hosted as an ordinary coroutine, plus a `legacy.*` command library.** Rejected: compiling to Luau source (no `goto`, 200-local and 255-upvalue limits, liveness analysis across labels that share `.@` state, plus dynamic features that defeat static compilation: `donpcevent` ~1,380 uses, `getd`/`setd` ~560, `getvariableofnpc` 75). Rejected: embedding the old bytecode VM (second VM, second sandbox). The cost of interpreting is irrelevant for NPC dialogue, which waits on client packets. Straight-line DB-embedded scripts (the 29,585 `Script` strings, mostly `bonus` lines) are the one place compiling to Luau is simple, and they compile to bonus tables.

- **Parser** for the declaration forms (`script`, `function`, `duplicate`, `shop` variants, `warp`, `monster`, `mapflag`) and the script grammar (statements, `switch`, loops, `goto`, labels, expressions). Parse once, cache as binary, invalidate by file hash.
- **Control flow:** the AST is lowered to a flat statement list with a program counter, so `goto`, `callsub` and fall-through `switch` are `pc` assignments and explicit frame objects (heap frames, no Luau recursion limit).
- **Blocking commands** (`next`, `close2`, `menu`, `select`, `prompt`, `input`, `sleep`, `sleep2`, `progressbar`) are coroutine yields of the interpreter loop.
- **Graceful degradation:** a command with no implementation (battleground, auction, anything outside the supported set) raises a typed `unsupported` error at that NPC only. The corpus load reports a per-file list of unsupported commands and disables just those NPCs.
- **Variables:** sigils map to namespaced stores under mod id `legacy`: `name` → character, `@` → temp character table, `$` → world, `$@` → temp world, `#` → account, `##` → global account, `.` → NPC object table (shared by `duplicate` clones if Q-H confirms), `.@` → Luau locals per call frame, `'` → instance table. Sparse array semantics (storing 0/"" deletes) are reproduced by the `legacy.array` type. Param variables (`Zeny`, `BaseLevel`) use a shim over attributes.
- **Commands:** the 388 used in `npc/` first, ordered by usage; the other 283 on demand. Each is classified `pure`, `reads world`, `writes world`, `yields`.
- **Events:** `On*` labels register bus hooks with the legacy names. The 2-slot player event queue and 100 ms dequeue are preserved behind a compatibility flag.
- **Safety:** command budget maps to the instruction budget; `freeloop` needs `unbounded_loop`; `atcommand`, `query_sql`, `setunitdata` are permission-gated.
- **Embedded DB scripts** (`Script`, `EquipScript`, `UnEquipScript`, achievement `Condition`: 30k strings) are the straight-line case: they compile to Luau/bonus tables (`bonus*` becomes compiled bonus deltas, not live memory writes), falling back to the interpreter for anything with control flow.

## 8. Tooling

- **`rathena-sim`** links the engine with a virtual clock, deterministic RNG and in-memory persistence. A `SimPlayer` speaks codec-level packets, no sockets. Scenarios are Luau: `local p = sim.player{job="knight", level=99}; p:cast("sm_bash", mob); assert(...)`. Tests carry `BR-RULESET-nnn` / `BR-*` tags, so coverage per rule is reportable.
- **Conformance:** (a) the legacy golden-trace harness (`RATHENA_GOLDEN_TRACE`) provides inputs and expected outputs for both rulesets; (b) differential test: each native stage vs its pure-Luau reference (only the stages that have a native version); (c) legacy script corpus: command-trace record/replay of the legacy VM against the Luau-hosted interpreter for each NPC, with the corpus-wide report of unsupported commands.
- **`rathena-migrate` (YAML migrator):** reads `db/{re,pre-re}` + `db/import*` with the legacy merge semantics (last file wins per field, `Index` list patching, `Mode` imports), validates against the legacy schema, emits Luau data modules into the two ruleset packages, routes embedded scripts through the script front end, and writes a diff report with a shared-by-diff section between the two rulesets.
- **`rathena-migrate-saves` (save-format migration):** converts legacy `char`, `inventory`, `skill`, `sc_data` (`val1..val4` to the named `fields` of each status, per-status mapping table), `acc_reg_*`/`char_reg_*` (sigils to namespaced KV under mod `legacy`), cart, storage, guild, party and mail. It has a **dry-run mode, per-table row-count and checksum reconciliation, and a rollback path** (the legacy database is opened read-only and never modified). This is the highest data-loss risk in the project, so it gets its own acceptance tests with real legacy dumps, and statuses it cannot map are parked as `orphaned`, never dropped.
- **`rathena-pack`:** emits client-side tables for mod-defined skills, items and status icons (§6.2).
- **Codegen:** `proto/*.yaml` → C++ codecs and sim client; `api/*.yaml` → bindings and `.d.luau`. Generated files are not edited.

## 9. Reuse from the earlier scaffold and legacy

| Source | Reuse |
|---|---|
| `modernized/rathena-reimagined/persistence` | SQLite/PostgreSQL connection layer, portable SQL subset, migrate tool. Repositories for gameplay aggregates are **not** reused: persistence is generic here |
| `.../core` | Config overlays, redaction logging, injectable `Rng`, typed config pattern. `BattleConfig` knobs become ruleset config |
| `.../netproto` | Framing and phase-gating tests; the hand-written inter-server tables are **dropped** with the topology change |
| `.../login-server` | Auth order, ticket, ban counter, Argon2id logic move into `gateway` as rules/config with their tests |
| `.../map-rules`, `.../char-rules` | Golden vectors and rule logic seed the rule packages; pure-C++ rules are ported to Luau and tested against the same vectors |
| legacy `path.cpp`, `timer.cpp`, `map.cpp` area semantics | Lifted with their behavior pinned by tests |
| `src/map/script_host.hpp`, `tools/ci/script_corpus/` | Seed the compatibility test corpus |

## 10. Units to scaffold (Phase E)

| Unit | Contents |
|---|---|
| `engine-core` | ecs, attrs, grid, path, tick, movement, instances |
| `native-defaults` | named versioned native stages (hot damage stages, default mob AI), opt-in by ruleset |
| `netproto` | spec format, codegen, session codec sets, obfuscation, gating, sim client |
| `script-host` | Luau embedding, bus, coroutines, budgets, sandbox, reload, storage API, API schema + `.d.luau` generator |
| `persistence` | generic records and KV, write-behind, migrations (lifts the earlier connection layer) |
| `gateway` | login, tickets, char-select, account and character lifecycle plumbing |
| `mods` | mod loader, manifest, dependency/permission resolution, id map, `rathena.base` |
| `ruleset-renewal` | renewal package: pipeline order, tables, status/skill/item definitions (starter set + conformance suite) |
| `ruleset-prerenewal` | pre-renewal package, same shape |
| `legacy-npc` | `.txt` parser + cached AST + Luau-hosted interpreter + `legacy.*` runtime; DB-script compiler for straight-line scripts |
| `tooling` | `rathena-sim`, `rathena-migrate`, codegen drivers |

## 11. Traceability (capability → home)

| Capability | Home |
|---|---|
| CAP-01…04 auth, admin, abuse, session | `gateway` (mechanism) + `rathena.base` (policy/hooks) |
| CAP-05…06 character lifecycle, PIN | `gateway` + `rathena.base` |
| CAP-07 persistence | `persistence` |
| CAP-08…12 parties, guilds, clans, mail, companions | `rathena.base`/content mods (P0 parties; guilds, mail P1) |
| CAP-13 world | `engine-core` + `legacy-npc` |
| CAP-14…18 combat, progression, skills, items, trade | ruleset packages, with `native-defaults` stages where measured necessary |
| CAP-19, 21, 22, 25 chat, GM, web, logging | content mods; web as `http.route` mod |
| CAP-20 anti-cheat | `netproto` gating (framing, phase) + `rathena.base` hooks |
| CAP-23 static data | `rathena-migrate` output in ruleset packages |
| CAP-24 client versions | `netproto` multi-version codecs |
| CAP-E01…E11 | as in §4–§8 |

## 12. Milestones (revised)

M0 runs **first and gates the design forks**. HITL #2 approval authorizes skeleton scaffolding and M0 only; M1+ build-out of the forked parts waits on the M0 go/no-go.

0. **M0 – Measurement spikes, with a written go/no-go:**
   - (a) **Damage:** pure-Luau weapon and magic pipeline, budget interrupt **on**, on AoE burst scenarios (e.g. 50 targets in one tick, 1,000 players + 500 mobs at 20 ms), both modes, against golden traces. Output: µs per hit. *If within budget, damage ships without native stages (§1.1).*
   - (b) **Legacy scripts:** the Luau-hosted interpreter on the worst-case files chosen by metric, not popularity: `enchan_mal.txt` (188 goto/callsub lines), `invest_main.txt` (167), `quests_16_1.txt` (18,841 lines), `agit_main_se.txt` (`freeloop`, dynamic events), plus `prontera.txt`. Output: pass/fail of command-trace replay, parse time, memory.
   - (c) **Hooks at scale:** Luau dispatch cost per movement event at 1,000 CCU with 3 hooks; sandbox/memory-accounting feasibility (allocator tags vs per-mod state).
   - (d) **Numeric fidelity:** the `ctx` int64/f32 method approach reproduces the E-1…E-10 worked examples exactly.
1. **M1 – Engine skeleton:** ecs, grid, path, tick, instance primitive, sim harness; **one client `(date, flavour)`** end to end (connect, walk, see others). Multi-version is architected (codec table keyed by version) but only one table is generated first.
2. **M2 – Script host + mods:** bus, coroutines, budgets, generations/reload, storage, loader, permissions, sandbox suite.
3. **M3 – Pre-renewal ruleset end to end** (smaller: 1,228 skills, 527 NPC files) with the conformance suite; mob AI; migrator for `db/pre-re`.
4. **M4 – Legacy NPC runtime** full, migrator full, save migration with dry-run.
5. **M5 – Renewal ruleset as the architecture proof.** Exit criterion: renewal is mostly data diff plus a handful of stage overrides, with no changes to the engine or `rathena.base` mechanism. If it needs engine changes, they are design bugs to fix first.
6. **M6 – Gateway/accounts/characters** hardening and legacy MySQL import (can run in parallel from M2).
7. **M7 – Remaining P0 breadth,** then P1 (guilds, mail, web routes).

Both rulesets remain P0 as you decided; the sequencing makes pre-renewal the first complete one.

## 13. Key risks

| Risk | Mitigation |
|---|---|
| Scripted damage too slow at AoE burst scale | M0(a) measurement; opt-in `native-defaults` stages for the hottest stages only |
| Luau numeric fidelity (doubles) | `int64` in native `ctx`, method API, golden traces, Q-B decision |
| Legacy giant/goto-heavy NPC files | AST interpreter with heap frames; M0(b) on worst-case files |
| Two rulesets drift or `rathena.base` grows rules | Mechanism-only rule, CI reference check, migrator diff report, M5 exit criterion |
| ID/number stability, client presentation of new content | String IDs in saves, tombstones, `rathena-pack` |
| Mod isolation (CPU/memory/escape) | Per-mod accounting, bounded natives, frozen shared tables, sandbox test suite, permissions, kill switch |
| Save-format migration data loss | Dry-run, reconciliation, read-only legacy DB, `orphaned` parking |
| Hook-induced non-determinism | Single sim thread, stable ordering, per-hook RNG streams, transactional effect queue |
| Single sim thread is the bottleneck | M0(c) numbers; gateway isolated; sharding seam kept |
| Scope: new engine plus two rulesets plus compat plus tooling | M0 gate, sequencing in §12, one client version first, dropped capabilities explicit |
| Native crash in a network-facing process hosting third-party code | Generated, fuzzed codecs (libFuzzer on every decoder), ASan/UBSan in CI, generated bindings |

## 14. Critic disposition and decisions

**Language: C++20** (not Rust). For: Luau's embedding API is C++, the reusable code (`path.cpp`, `timer.cpp`, the earlier scaffold) is C++, and contributors know it. Against: Rust's memory safety is attractive for a network-facing server that runs third-party code. Mitigation as listed in the risks table. If the team prefers Rust and accepts losing the reuse, it is defensible; the codec and API generators are language-neutral.

| Finding | Disposition |
|---|---|
| B1 declarative executor | **Accepted.** Replaced by `native-defaults` (§1.1), gated on M0(a). |
| H1 topology | **Partly accepted.** One binary, `--role`, isolated gateway thread pool, copy-on-write persistence, failure-mode table (§3). A true multi-process split stays a seam. Q-K still needs an operator answer. |
| H2 compile `.txt` | **Accepted.** Luau-hosted AST interpreter (§7); gated on M0(b). |
| H3 hot reload | **Accepted.** Generations, orphaned definitions, timeout, tests (§5). |
| H4 numerics/perf | **Accepted.** `ctx` int64 methods, per-hook RNG streams, degradation policy (§5), M0(a)(d). Q-B recommended answer below. |
| H5 IDs | **Accepted** (§6.2). |
| H6 `rathena.base` | **Accepted** (§6.3). |
| H7 scope | **Partly accepted.** You chose both rulesets as P0, so both stay; sequenced pre-renewal first with renewal as the proof (§12). One client version first. |
| M1 language | Recorded above. |
| M2 mod security | **Accepted** (§5). |
| M3 instances, BG, saves, observability, protected stages | **Accepted** (§4, §5, §7, §8). Dangling "persistence note" is now `rathena-migrate-saves`. |
| M4 ordering/cancel | **Accepted** (§5). |
| M5 aggregator | **Accepted** (§4 `attrs`). |
| N1–N5 | N1: lint is a smoke check only, plus a review rule on numeric literals in the generic core. N2: gateway rehashes legacy plaintext/MD5 passwords to Argon2id on first successful login. N3: `rathena-pack` is defined (§8). N4: M0 now precedes build-out. N5: no injection findings. |

## 15. Decisions needed at HITL #2

1. **Approve the architecture** (or request changes), including the one-binary topology with isolated gateway.
2. **Native fast path wording (the one place your vision pulls against itself):** the generic core has no game rules, while the default damage calc is a *plug-in library the ruleset opts into*. OK, or must it contain no formulas at all (which would mean dropping native damage and keeping only mob AI native)? M0(a) may make this moot if pure Luau is fast enough.
3. **Parity (Q-B), my recommendation:** bit-exact for the default pipelines (golden traces including float32 steps and RNG draw counts) under `strict_parity`; third-party hooks use their own RNG streams. Alternative: statistical parity (cheaper, but golden traces then become tolerance checks).
4. **Dropped for the first milestone (Q-A):** auction and battlegrounds, with the NPC corpus degrading gracefully. Confirm.
5. **Renewal sub-flags (Q-C):** bundles by default (`ruleset.renewal`, `ruleset.prerenewal`), sub-flags as unsupported-advanced overrides. Confirm.
6. **Legacy defects (Q-G):** ship as-is behind a `legacy_quirks` flag (default on for parity), fix later? Confirm.

## 16. HITL #2 outcome (recorded 2026-10-18)

The user replied "continue with your own recommendations". That is recorded as **approval of architecture v2 with my recommended answer on each §15 item**:

1. Architecture v2 approved, including the one-binary topology with `--role` and isolated gateway.
2. Native fast path = `native-defaults` plug-in the ruleset opts into; the generic core stays formula-free. Native damage stages are built only if M0(a) shows pure Luau misses the budget.
3. Parity: bit-exact for default pipelines under `strict_parity`; third-party hooks use their own RNG streams.
4. Auction and battlegrounds are out of the first milestone; the NPC corpus degrades gracefully.
5. Renewal sub-flags: two bundles by default; sub-flags are unsupported advanced overrides.
6. Legacy defects ship as-is behind a `legacy_quirks` flag (default on).

Open and still needing an operator answer: Q-K (more than one map process in production?), Q-I (use of the 283 never-used commands, `query_sql`, `atcommand`, `setunitdata`), Q-H (`.` variable sharing across `duplicate()` clones), Q-J (event-queue drop contract).
