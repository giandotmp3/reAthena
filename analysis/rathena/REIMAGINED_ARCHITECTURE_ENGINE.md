# rAthena Engine + Luau Mods: Reimagined Architecture (DRAFT v1, pre-critique)

**Spec:** `AI_NATIVE_SPEC.md` (base) + `AI_NATIVE_SPEC_ENGINE.md` (delta) + `BR_RULESET.md` (119 ruleset rules)
**Supersedes for this vision:** `REIMAGINED_ARCHITECTURE.md` (in-place C++20 refactor). That document's decision K2 (keep the legacy NPC VM) is **reversed** here: the legacy VM is replaced by Luau plus a compatibility compiler. Its `modernized/rathena-reimagined/` scaffold is untouched; reusable parts are listed in §9.
**Status:** awaiting critic review, then HITL #2.

## 1. Principle

> The engine knows *how things move, connect, persist and are scheduled*. It does not know *what anything is*.

Everything with a game name (skill, item, status, job, element, formula, quest, map rule) is a **definition** in a Luau package. The engine sees only opaque namespaced IDs, generic attributes, generic component data, events and hooks.

### 1.1 How "native fast paths" stay rule-free

A native default damage calculation with hard-coded constants would break the principle. Resolution:

- The ruleset package publishes its pipeline as **declarative data**: ordered stage list, per-stage *operation programs* from a small closed op set (`add`, `mul_pct`, `table_lookup`, `curve`, `clamp`, `floor_div`, `f32_round`, `rng_range`, `min1`…), and the tables they use (element matrix, size matrix, defense curve parameters).
- The engine ships a **native executor** for these programs. It has no numbers of its own.
- A stage with no script hook runs through the native executor. A stage with a hook runs the Luau hook, and its result feeds the next stage.
- Conformance rule: for every pipeline, the native-compiled path and the pure-Luau path must give identical results (differential test, §8). Behavior never depends on which path ran.
- Mob AI follows the same pattern: a native state machine (idle, chase, attack, return, berserk…) driven by mode bits and tunables from the mob definition. Scripts supply mob-skill conditions and events. Engine contains no mob-specific rule.

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
    Container(sim, "sim core", "C++20", "ECS store, map grid, spatial queries, A*, tick loop, timers, native executors")
    Container(host, "script host", "C++20 + Luau", "VM, event bus, coroutines, budgets, sandbox, hot reload, storage API")
    Container(persist, "persistence", "C++20", "Write-behind store, KV, record collections, migrations")
    Container(mods, "mods/ (Luau packages)", "Luau + data", "ruleset.renewal / ruleset.prerenewal, content, legacy npc, user mods")
    Container(legacy, "legacy-npc", "Compiler + Luau runtime lib", ".txt NPC scripts -> Luau; 671-command shim")
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

Threads: network IO threads decode frames only. **One simulation thread** owns the ECS, the Luau VM and all rules (deterministic, no locks). Persistence runs on worker threads fed by dirty-set snapshots. Hashing (Argon2id) runs on a bounded pool.

## 4. Native core (no game knowledge)

| Module | Responsibility | Rule-free by |
|---|---|---|
| `ecs` | Generational entity ids, sparse-set component pools, archetype-free queries, deferred structural changes | Components are engine-defined (`Position`, `Facing`, `Mover`, `Session`, `Collider`, `Persisted`) or **script-declared schemas** (typed fields, registered at load) |
| `attrs` | Generic attribute system: interned attribute ids, contribution lists `(source, op, value, order_slot)`, incremental recompute by dirty attribute | The ruleset names the attributes (`atk`, `def`…) and ops. Engine only orders and applies `override > add > mul_pct > clamp` within slots |
| `grid` | Cell flags (static and dynamic, ids registered by rulesets), 8×8 buckets, area queries with legacy semantics (snapshot-then-call, mob pass order, optional wall-check) | Cell flags are bits; the ruleset gives them meaning |
| `path` | A\* with legacy tie-breaks (Manhattan×10, diagonal 14, no corner cutting), `path_search_long`, knockback cell search | Pure geometry. Limits (`32`, official-walkpath 14) become engine config |
| `tick` | Single-thread loop, min-heap timers, 20 ms floor, late-timer catch-up rules, virtual clock for tests | Timers carry a callback id or Luau function ref |
| `net` / `netproto` | Sockets, session codec set selected by negotiated `(date, flavour)`, stateful obfuscation key, framing and phase gating | Codecs generated from `proto/*.yaml`; handlers are events (`net:CZ_REQUEST_MOVE`) |
| `persist` | Write-behind of dirty `Persisted` components and KV; schema migrations; online/session tables | Stores opaque typed records keyed by `(owner, kind)` |
| `gateway` | Login auth plumbing, account store, ticket (30 s, single use), char list/create/delete flow, PIN plumbing | Name policy, slot count, PIN complexity, ban order are **config + hooks** supplied by `base` mod (BR-LOGIN/CHAR rules live in script or config, validated by the inherited acceptance tests) |
| `native executors` | Damage pipeline executor, stat aggregator, mob AI state machine, ground-unit ticker | Run ruleset-published declarative programs and tables (§1.1) |
| `movement` | Walk request → path → per-cell step timers → broadcast; fires `unit:before_move` / `unit:moved` hooks | Replaces rules embedded in legacy `map_moveblock` with an on-move hook |

The engine never includes a header named after a skill, status, item, job or map flag. A CI lint scans `engine/` for the identifier lists extracted from `skill.hpp`, `status.hpp`, `itemdb.hpp` and `mmo.hpp` and fails on any match.

## 5. Script host (Luau)

- **VM:** one Luau state; each mod loads into its own **sandboxed environment** (`luaL_sandbox` style, read-only shared globals, no `os`, `io`, `debug`, `require` of arbitrary paths). Cross-mod calls go through declared `exports`.
- **Event bus:** names are `namespace:event`. Hook = `{id, mod, priority, after/before constraints, phase: pre|on|post, mode: observe|modify|replace}`. Dispatch order is by priority, then topological constraints, then load order, then registration sequence (stable). Any `pre` hook may `cancel(reason)`. Payload is a snapshot plus a mutable `ctx` for `modify`. **World mutation inside a hook is an enqueued effect**, applied by native code after the stage settles (BR-RULESET-116), with a re-entrancy guard.
- **Coroutine dialogue:** `say`, `choose`, `input`, `wait` yield to the engine and resume on the client packet or a timer. Per-player dialogue state is a coroutine plus its resume token; a disconnect cancels it. Idle dialogs time out.
- **Budgets and sandbox:** Luau interrupt callback enforces an instruction budget per event dispatch, per coroutine slice and per tick (total script time per tick is capped). Exceeding a budget aborts that script with a trace, never the tick. A `unbounded_loop` permission is the `freeloop` equivalent.
- **Hot reload:** at a tick boundary, a mod is re-evaluated into a new environment. In-flight coroutines keep the old closures until they finish. Hooks are re-registered atomically. Definitions are diffed by namespaced id; removed definitions mark dependents invalid and surface in the dev console.
- **Storage:** `storage.character(ch):mod("id")`, `storage.account(...)`, `storage.world()`. Typed values (int64, double, string, bool, nested table). Namespaced by mod id automatically. Per-mod quota. Write-behind with versioned rows. Cross-mod access needs the `storage.read:<mod>` permission. This replaces the sigil variable system.
- **Typed API:** one schema (`api/*.yaml`) generates both the C++ bindings and `engine.d.luau`; `luau-analyze` checks every mod in CI. Editor autocomplete uses the same definitions.
- **Numeric fidelity:** `num.idiv`, `num.i64`, `num.f32`, `rng` with legacy draw-count behavior (BR-RULESET-115).

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
- Others are allocated from a reserved range per kind and **persisted in an `id_map` table** so numbers are stable across restarts and mod removal.
- Collision is a load error naming both mods.

### 6.3 Ruleset packages

- `ruleset.renewal` and `ruleset.prerenewal` are **separate packages**, mutually exclusive via `conflicts` and `provides = ["ruleset"]`.
- Shared helper code lives in a third package `rathena.base` (utilities, shared tables, common conformance helpers) that both depend on. This keeps the two rulesets independent while avoiding duplicated plumbing. Whether any *game content* is shared is a deliberate question (see critic notes).
- Package config carries the six legacy sub-flags (`cast`, `drop`, `exp`, `lvdmg`, `aspd`, `stat`) as defaults per package; the operator can override each (Q-C).

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

The native stat aggregator reads `contributes` once at definition time (functions are compiled to closed op programs where possible; otherwise evaluated lazily and cached per `(entity, status_version)`).

### 6.6 Skills and items

Skills are definitions with `cast`, `requires`, `target`, `hooks = { on_cast_end, calculate_ratio, additional_effect, … }`, covering the nine legacy `SkillImpl` hook points. Items define `equip_bonus` (compiled bonus deltas, replacing `bonus`/`bonus2` script calls), `on_use`, `on_equip`, combos. The ~506 `SP_*` bonus vocabulary becomes the `bonus.*` function table in `rathena.base`.

## 7. Legacy NPC compatibility (`legacy-npc`)

**Choice: compile `.txt` → Luau at load time (cached), plus a Luau runtime library (`legacy.*`) implementing the commands.** Rejected: interpreting the old bytecode inside the new host (keeps two VMs and two sandboxes).

- **Parser** for the declaration forms (`script`, `function`, `duplicate`, `shop` variants, `warp`, `monster`, `mapflag`) and the script grammar (statements, `switch`, loops, `goto`, labels, expressions).
- **Control flow:** Luau has no `goto`. Functions that use labels/`goto`/`callsub` compile to a dispatch loop (`local pc = "L_entry"; while true do if pc == ... end`), others compile structurally.
- **Blocking commands** (`next`, `close2`, `menu`, `select`, `prompt`, `input`, `sleep`, `sleep2`, `progressbar`) are coroutine yields.
- **Variables:** sigils map to namespaced stores under mod id `legacy`: `name` → character, `@` → temp character table, `$` → world, `$@` → temp world, `#` → account, `##` → global account, `.` → NPC object table (shared by `duplicate` clones if Q-H confirms), `.@` → Luau locals per call frame, `'` → instance table. Sparse array semantics (storing 0/"" deletes) are reproduced by the `legacy.array` type. Param variables (`Zeny`, `BaseLevel`) use a shim over attributes.
- **Commands:** the 388 used in `npc/` first, ordered by usage; the other 283 on demand. Each is classified `pure`, `reads world`, `writes world`, `yields`.
- **Events:** `On*` labels register bus hooks with the legacy names. The 2-slot player event queue and 100 ms dequeue are preserved behind a compatibility flag.
- **Safety:** command budget maps to the instruction budget; `freeloop` needs `unbounded_loop`; `atcommand`, `query_sql`, `setunitdata` are permission-gated.
- **Embedded DB scripts** (`Script`, `EquipScript`, `UnEquipScript`, achievement `Condition`: 30k strings) go through the same compiler. The `bonus*` commands compile to compiled bonus tables, not live memory writes.

## 8. Tooling

- **`rathena-sim`** links the engine with a virtual clock, deterministic RNG and in-memory persistence. A `SimPlayer` speaks codec-level packets, no sockets. Scenarios are Luau: `local p = sim.player{job="knight", level=99}; p:cast("sm_bash", mob); assert(...)`. Tests carry `BR-RULESET-nnn` / `BR-*` tags, so coverage per rule is reportable.
- **Conformance:** (a) the legacy golden-trace harness (`RATHENA_GOLDEN_TRACE`) provides inputs and expected outputs for both rulesets; (b) differential test: native-executed pipeline vs pure-Luau pipeline; (c) legacy script corpus: command-trace record/replay of the legacy VM against the compiled Luau for each NPC.
- **`rathena-migrate` (YAML migrator):** reads `db/{re,pre-re}` + `db/import*` with the legacy merge semantics (last file wins per field, `Index` list patching, `Mode` imports), validates against the legacy schema, emits Luau data modules into the two ruleset packages, routes embedded scripts through the compiler, and writes a diff report. It also converts SQL character data from the legacy schema (see the persistence note).
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
| `engine-core` | ecs, attrs, grid, path, tick, movement, native executors |
| `netproto` | spec format, codegen, session codec sets, obfuscation, gating, sim client |
| `script-host` | Luau embedding, bus, coroutines, budgets, sandbox, reload, storage API, API schema + `.d.luau` generator |
| `persistence` | generic records and KV, write-behind, migrations (lifts the earlier connection layer) |
| `gateway` | login, tickets, char-select, account and character lifecycle plumbing |
| `mods` | mod loader, manifest, dependency/permission resolution, id map, `rathena.base` |
| `ruleset-renewal` | renewal package: pipeline order, tables, status/skill/item definitions (starter set + conformance suite) |
| `ruleset-prerenewal` | pre-renewal package, same shape |
| `legacy-npc` | `.txt` parser + Luau emitter + `legacy.*` runtime |
| `tooling` | `rathena-sim`, `rathena-migrate`, codegen drivers |

## 11. Traceability (capability → home)

| Capability | Home |
|---|---|
| CAP-01…04 auth, admin, abuse, session | `gateway` (mechanism) + `rathena.base` (policy/hooks) |
| CAP-05…06 character lifecycle, PIN | `gateway` + `rathena.base` |
| CAP-07 persistence | `persistence` |
| CAP-08…12 parties, guilds, clans, mail, companions | `rathena.base`/content mods (P0 parties; guilds, mail P1) |
| CAP-13 world | `engine-core` + `legacy-npc` |
| CAP-14…18 combat, progression, skills, items, trade | ruleset packages, with native executors |
| CAP-19, 21, 22, 25 chat, GM, web, logging | content mods; web as `http.route` mod |
| CAP-20 anti-cheat | `netproto` gating (framing, phase) + `rathena.base` hooks |
| CAP-23 static data | `rathena-migrate` output in ruleset packages |
| CAP-24 client versions | `netproto` multi-version codecs |
| CAP-E01…E11 | as in §4–§8 |

## 12. Migration plan

1. **M0 – Spikes (before any build-out):** (a) compile 20 representative large NPC files (prontera, a 3-1 job quest, `agit_main_se`) to Luau and replay command traces; (b) run the damage pipeline for 10 skills in both modes natively and in Luau against golden traces, and measure ns per hit; (c) measure Luau dispatch cost at 1,000 CCU for movement hooks.
2. **M1 – Engine skeleton:** ecs, grid, path, tick, sim harness, one client version end-to-end (connect, walk, see others).
3. **M2 – Script host + mods:** bus, coroutines, budgets, reload, storage, loader.
4. **M3 – Rulesets:** pipeline, status aggregation, first 40 skills, items, mob AI; golden-trace conformance.
5. **M4 – Legacy NPC compiler;** migrator; full DB migration.
6. **M5 – Gateway/accounts/characters,** persistence import from legacy MySQL.
7. **M6 – Remaining P0 breadth,** both rulesets at parity on the conformance suite. Then P1.

## 13. Key risks

| Risk | Mitigation |
|---|---|
| Script cost per hit/tick at scale | Native executors (§1.1); M0 measurement; per-tick script time cap |
| Luau numeric fidelity (doubles) | `num.*` helpers; golden traces; Q-B decision |
| `.txt` → Luau compile of giant files (Luau function and constant limits, no `goto`) | Split per label; dispatch loop; M0 spike on the largest files |
| Two rulesets drift | Shared `rathena.base`; shared scenario suite run against both |
| Mod ID/number stability | Persisted `id_map`, explicit `client_id` for migrated content |
| Hook-induced non-determinism | Single sim thread, stable ordering, effect queue |
| Scope: this is a new engine plus a ruleset port | Milestones above; P0 breadth is still large (see critic notes) |
