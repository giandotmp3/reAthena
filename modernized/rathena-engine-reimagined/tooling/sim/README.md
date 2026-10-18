# rathena-sim

Headless test harness for the rathena engine (architecture section 8). One binary, `rathena-sim`, and the
library behind it (`rathena_tooling`, sources `src/sim_*.cpp`, headers `include/tooling/sim_*.hpp`).

## Build and run

    export RATHENA_LUAU_ROOT=/path/to/luau/src RATHENA_LUAU_LIB_DIR=/path/to/luau/build   # as script-host/README.md
    cmake -S tooling -B build-sim && cmake --build build-sim -j
    build-sim/tooling_tests                 # all tooling tests; the sim ones need Luau and report SKIP without it
    build-sim/tooling_tests --only TL-SIM-0 # tests whose "RULE name" contains the text
    build-sim/rathena-sim help

Needs Python 3 + PyYAML (the netproto catalog is generated into the build tree, like netproto's own build does)
and the system SQLite. Sanitizers: configure with
`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" -DTOOLING_SANITIZE=ON`.
Nothing is written into the sibling units: packages are copied into a per-Sim work directory under the system
temp dir (removed on exit; honours `TMPDIR`).

| Command | What it does |
|---|---|
| `run <scenario.luau \| dir>... [--seed N] [--json] [-v]` | each scenario in a fresh Sim built from its `--@` directives; exit 1 if any fails |
| `coverage [--json \| --json-out FILE] [--suites] [--no-run] [--no-static] [--test-output F]... [--tests-bin P]... [--require SPEC] [--require-level passing\|covered]` | BR-ID coverage; exit 0 unless `--require` is given and not met (2 for an unknown id) |
| `conformance --ruleset prerenewal\|renewal [--json] [--no-suite] [--no-golden] [--subset N]` | golden suites of a ruleset package through the sim host; exit 1 on any mismatch |
| `load [--players N] [--seconds S] [--seed N]` | N random-walking, chatting SimPlayers; reports wall seconds per virtual second |
| `mods` | mods found under the engine tree |

## What is wired (and what is the real thing)

| Piece | From | How the sim uses it |
|---|---|---|
| ECS, grid, spatial index, A*, tick/timers with a virtual clock, movement + on-move hooks | `engine-core` | one map, `engine::VirtualClock` + `TimerQueue`; `Sim::step()` advances one tick (20 ms), `run_for(ms)` many; movement steps are engine-core's step timers (`walk_step_ms` 150 is the sim's number, not the engine's) |
| One `ModState` per mod, `EventBus`, effect queue, dialogue manager, reload generations, timers, storage API, `RngStreams` | `script-host` (real Luau 0.690) | mods are loaded into it by the loader; the bus is dispatched for `unit:before_move`, `unit:moved`, `sim:chat`, `sim:enter` when a mod registered a hook; `DialogueManager` runs legacy NPC scripts; `ReloadManager` does hot reload |
| mod.toml, resolver, mods.lock grants, id map, `HostRuntime`, kill switch | `mods` | `Sim` discovers `mod.toml` under the engine tree (+ `--@mods-dir`), adds dependencies, builds the `LoadRequest` and loads it with `ModLoader::load`. A package is *staged* first (copied, plus a generated `sim/export.luau`, see below) |
| `legacy.npc`, `rathena.base`, `ruleset.prerenewal`, `ruleset.renewal` | their packages | loaded as mods; NPC `.txt` files from `npc/` are loaded with `Sim::load_npc_file` and run by the Luau interpreter inside `legacy.npc` |
| `KvStore` + `KvStorageBackend` over SQLite (`:memory:` or a file) | `persistence` | script-host's `StorageApi` writes through it; `SimConfig::sqlite = false` selects script-host's in-memory backend |
| framing (`peek_frame`), phase gating, 3-packets-per-cycle budget, connect checks, pos3/pos6 packing, generated catalog of the real spec | `netproto` | the session boundary parses the SimPlayers' byte streams with netproto's own functions |
| seeded RNG | `script-host` `RngStreams` (Luau side: main stream + per-hook streams) and `tooling` `DeterministicRng` (native side) | see Determinism |

### Staging, and why `eval` exists

The mods bundler wraps a package into one Luau chunk whose modules are locals, and names modules with
`fs::relative` (which resolves symlinks), so the sim copies each package into `work/stage/<mod>/` and adds
`sim/export.luau`:

    SIM_REQUIRE = require   SIM_IMPORT = import   SIM_MOD = "<id>"   (legacy.npc also: LEGACY = require("luau/legacy"))

`Sim::eval(mod, source)` runs a chunk in that mod's own state and returns what it printed; the package's modules are
`SIM_IMPORT("scripts/damage/weapon")`. This is how the damage scenarios call the ruleset pipeline "through the
ruleset package". Ruleset staging options: `golden` (None/Subset/Full; the recorded legacy cases as
`build/golden_weapon_<pre|re>.luau`), `stage_test_support` (the package's `tests/support`), `stage_package_tests`
(its whole `tests/` and `conformance/`). When the unit's own expansion of the golden data is missing,
`sim/expand_golden.py` produces the identical file (checked byte for byte) in the work directory.

## SimPlayer and the wire

`SimPlayer` (C++: `include/tooling/sim_player.hpp`; Luau: `sim.player{}`) builds the byte frames a client would send
and decodes the frames the server sends. No sockets. Everything between the two is `Sim::client_send/client_take`.

| Direction | Packets (opcode, length) | Origin |
|---|---|---|
| client to server | CZ_ENTER 0x0436/23, CZ_NOTIFY_ACTORINIT 0x007d/2, CZ_REQUEST_MOVE 0x035f/5, CZ_REQUEST_TIME 0x0360/6, CZ_REQUEST_CHAT 0x00f3/var, CZ_CONTACTNPC 0x0090/7 | in the netproto spec (`client_packets.yaml`); opcode, length, gate come from the generated catalog |
| client to server | CZ_REQ_NEXT_SCRIPT 0x00b9/6, CZ_CHOOSE_MENU 0x00b8/7, CZ_REQUEST_ACT2 0x0437/7, CZ_USE_SKILL 0x0438/10 | legacy packet DB only (handler-parsed, `fields: unknown` in netproto) |
| client to server | CZ_CLOSE_DIALOG 0x0146/6, CZ_INPUT_EDITDLG 0x0143/10, CZ_INPUT_EDITDLGSTR 0x01d5/var | typed structs in the legacy DB, not in the spec |
| server to client | ZC_NOTIFY_TIME 0x007f/6, ZC_NOTIFY_PLAYERMOVE 0x0087/12 | in the spec |
| server to client | ZC_ACCEPT_ENTER 0x02eb/13, ZC_REFUSE_ENTER 0x0074/3, ZC_STOPMOVE 0x0088/10, ZC_NOTIFY_VANISH 0x0080/7, ZC_NOTIFY_CHAT 0x008d, ZC_SAY_DIALOG 0x00b4, ZC_WAIT_DIALOG 0x00b5/6, ZC_CLOSE_DIALOG 0x00b6/6, ZC_MENU_LIST 0x00b7, ZC_OPEN_EDITDLGSTR 0x01d4/6, ZC_NOTIFY_ACT 0x08c8/34 | typed structs in the legacy DB, not in the spec |
| server to client | ZC_NOTIFY_STANDENTRY11 0x09ff/108, ZC_NOTIFY_MOVEENTRY11 0x09fd/114, ZC_NOTIFY_PLAYERCHAT 0x008e | untyped in the DB; layout of the two entry packets read from `struct packet_idle_unit` / `packet_unit_walking` in `src/map/packets_struct.hpp` (RE flavour, PACKETVER 20211103) |

`tests/test_sim_wire.cpp` checks every opcode and length of this table against `netproto/proto/legacy_20211103.yaml`
and the eight spec packets against the generated catalog (a changed length fails the test; verified by mutation).

**Limitations, stated plainly.**
- For the handler-parsed packets the spec has offsets (`pos[]`) but no types. The sim writes the fields where
  the legacy handler reads them and gives them the meaning the handler gives them (read from `clif.cpp`); that is the
  sim's reading, not netproto data. CZ_REQUEST_MOVE carries netproto's packed pos3 because `clif_parse_WalkToXY`
  reads it with `RFIFOPOS`.
- Opcode obfuscation, the other 154 shuffle blocks, the login server (CA_LOGIN), flood guard, version negotiation,
  compression and real timing/latency are not simulated. There is one client build, `re/20211103`.
- `CodecSet::decode/encode` and `SimClient` in netproto are still stubs, so the sim has its own small codec
  (`sim_wire.cpp`) for the packets above and uses netproto for framing, gating, packing and the catalog.
- ZC_STOPMOVE on arrival and ZC_NOTIFY_VANISH type 3 on logout are the sim's conventions (legacy sends a stop packet
  only when a walk is interrupted).
- `cast` sends CZ_USE_SKILL; the ruleset packages have no skill formulas, so nothing comes back (test TL-SIM-003 stays
  pending, strengthened to require a damage frame).

### What a SimPlayer can do

`connect()` (CZ_ENTER, ZC_ACCEPT_ENTER, CZ_NOTIFY_ACTORINIT, then in the world), `walk(x,y)`, `say(text)` (frames
`"<name> : <text>"`), `click_npc(name)`, `answer(n)` (menu choice, 255 cancels), `next()`, `input(text)`,
`input_number(n)`, `close_dialog()`, `ping()`, `attack(gid)`, `cast(skill, gid, lv)`, `send_raw(bytes)`, `disconnect()`;
`wait_for(event, timeout_ms)` advances virtual time until an unconsumed event arrives (or times out, returning nothing),
`expect(...)` throws naming the inbox, `expect_none(...)`, `drain_received()`, `last()`, `count()`, `sees(gid)`.
Event names are packet names or aliases (`say`, `next`, `menu`, `input`, `close`, `move`, `stop`, `appear`, `standing`,
`moving`, `vanish`, `chat`, `chat_self`, `damage`, `accept`, `refuse`, `time`).

The server side of the boundary (`sim_server.cpp`): netproto gating decides who may send what; a walk is
`MovementSystem::request_walk`; the area broadcast is a 14-cell Chebyshev query on the spatial index with symmetric
view sets (appear / vanish both ways); an NPC click starts a `DialogueManager` coroutine in `legacy.npc` and each
`say`/`choose`/`input`/`wait` request becomes the frames above; an attack runs the loaded ruleset's `calc_attack`
with the sim's main RNG stream.

## Scenarios

`scenarios/*.luau` run in a sandboxed script-host state of their own (mod id `sim.scenario`: no storage, no RNG, no
permissions, 200 M step budget). The `sim` global is Luau (`sim/scenario_prelude.luau`, embedded in the binary); each
call is an RPC over the dialogue channel (`input(prompt)` carries the request, the host answers with the result), so a
scenario cannot touch anything the host does not offer. Directives are comment lines at the top (full list in
`include/tooling/sim_scenario.hpp`): `--@rule`, `--@mods`, `--@mods-dir`, `--@seed`, `--@map`, `--@golden`,
`--@test-support`, `--@grant`, `--@memory-storage`, `--@npc`, `--@strict-parity`.

| Scenario | Covers |
|---|---|
| `two_players_see_each_other` | engine-core movement + area broadcast (TL-SIM-011) |
| `npc_dialogue_hypnotist` | `npc/pre-re/other/resetskill.txt` through legacy-npc, driven by SimPlayer answers (TL-SIM-020) |
| `damage_prerenewal_golden`, `damage_renewal_golden` | the pipeline through the ruleset package with the recorded legacy cases (BR-RULESET-020, 031, 037, 115, 119) |
| `example_bash` | E-3 end to end: CZ_REQUEST_ACT2 in, ZC_NOTIFY_ACT out (BR-RULESET-020) |
| `reload_during_select` | hot reload during a pending `select` (TL-SIM-005) |
| `reload_with_pending_status_tick` | reload with a pending status tick (TL-SIM-006) |
| `mod_hook_cancel_ordering`, `hook_rng_streams` | bus order, cancel, effects, kill switch, constraint cycle, per-hook RNG (BR-RULESET-115/116/117) |
| `chat_speaker_integrity`, `packet_gating` | BR-MAP-057, BR-MAP-059, BR-MAP-060 |
| `persistence_roundtrip` | position through SQLite (TL-SIM-030) |

Synthetic mods used by scenarios live in `scenarios/mods/` (each a `mod.toml` + `scripts/init.luau`).

## Determinism

Time moves only in `Sim::step/run_for`. Inbound frames are processed per tick in session order (3 per socket), then
engine timers, then script-host timers and dialogue time, in that order. Containers that decide order are sorted.
Native randomness (login ids, the load driver) comes from `DeterministicRng` derived per purpose
(`rng().derive("login:<name>")`); Luau randomness from `RngStreams(seed)`: `rng.main` is the legacy-parity stream
(mt19937, draws counted), `rng.roll` the calling hook's own SplitMix64 stream derived from (seed, mod, hook id). The
**trace** hashes every frame in and out (opcode, length, content hash), every hook dispatch and every reload.
`BR-RULESET-115` tests: equal seeds give equal hash, frames, bytes, draw counts; another seed gives another hash; a hook's
numbers do not change when another hook is added; hooks never move the main stream; under `strict_parity` a hook
that draws from the main stream fails with the parity error and the others still run. The hash is also equal between
the Debug, Release and ASan/UBSan builds. Limit: script-host does not count hook-stream draws (only the main stream),
so hook draws are checked through their values, not a counter.

## Coverage report

`rathena-sim coverage` reads the rule universe from the rows `| BR-xxx-nnn | ... |` of
`analysis/rathena/BR_RULESET.md`, `AI_NATIVE_SPEC.md` and `AI_NATIVE_SPEC_ENGINE.md` (310 rules: BR-RULESET 119,
BR-LOGIN 39, BR-CHAR 55, BR-MAP 61, BR-INTER 19, BR-WEB 17) and gathers evidence:

| Kind | Source | Executed? |
|---|---|---|
| `scenario` | `--@rule` / `-- rule:` of `scenarios/*.luau` and `sim.rule()` at run time | yes (unless `--no-run`) |
| `ruleset-suite`, `ruleset-golden` | both ruleset packages' own `tests/main` and golden replay under the sim host | yes, with `--suites` |
| `test-output` | `PASS/FAIL/XFAIL/XPASS/SKIP` (or `--list`) lines of a test binary (`--tests-bin`, `--test-output`) | the binary is run by `--tests-bin` |
| `static` | rule ids written in `tests/`, `conformance/`, `rules/`, `scenarios/` of every unit | no (status `tagged`, or `pending` when the line or file says so) |

A rule is `failing` (any failing evidence), else `passing`, `pending`, `tagged` (static only), or `uncovered`. Text
output has the per-family table, the passing rules with their tests, and the uncovered rules as ranges; `--json` /
`--json-out` give `{universe, summary, rules[{id,title,source,status,covered_by[]}], uncovered[], other_tags, notes}`.
`--require SPEC` (id, family prefix, `BR-X-001..010`, or `all`; repeatable) makes the exit status 1 when a required rule
does not reach `--require-level` (`passing` default, or `covered`). Without `--require` the exit status is 0 whatever the report says.

## Conformance driver

`rathena-sim conformance --ruleset prerenewal|renewal` loads `rathena.base` + the ruleset through `ModLoader`, runs
the package's own `tests/support/golden_runner` over its recorded legacy cases inside the mod's sandbox (real Luau,
not lupa), and prints match rates per category; with the suite on it also runs the package's `tests/main` under the
sim host. `replay_golden_trace()` replays `scenarios/golden_example.trace`-style records (`rule \t seed=N \t damage=1000
race=+20 size=+15 \t 1380`) through each package: the inputs are turned into a neutral unit per ruleset (documented in
`sim_conformance.cpp`), the package's `calc_attack` runs on a seeded stream, damage is compared.

## What the sim exercises, and what it does not

Exercised, for real: engine-core ECS/grid/path/timers/movement/spatial index; script-host sandboxes, bus, effect
queue, dialogue coroutines, reload generations and leases, timers, storage, RNG streams; the mod loader (resolve,
grants, bundling, kill switch); persistence's SQLite KV store behind script-host storage; netproto framing, gating,
connect checks and packing; the legacy.npc interpreter on real `npc/*.txt` files; the ruleset packages' Luau pipelines
against recorded legacy results.

NOT exercised: **there is no gateway and no network** (no login server, tickets, char-select, sockets, TLS, flood guard,
opcode obfuscation, version negotiation, latency, packet loss); the world has one map and one process; persistence is not
driven through write-behind, WAL or PostgreSQL (write-through SQLite only); the damage pipeline runs against the ruleset
package's pure-Luau fake `ctx` (script-host's native `ctx` lacks `acc`/`rng` methods the pipeline needs) and only for the
basic weapon attack; no skills, statuses, items, mobs, AI, party/guild/trade, instances, `OnTouch`/clock events or
multi-process behaviour; `register(engine)` of the packages is not called (script-host defines no `engine` global);
unit snapshots for combat are supplied by the scenario, not derived from jobs/stats/equipment; the dialogue manager's
requests do not distinguish `input` from numeric `input`, so the sim always opens the string dialog.

## Decisions where the spec was silent

- Tick 20 ms (the engine's floor); view range 14 (legacy `AREA_SIZE`); walk step 150 ms (a supplied game number).
- Server tick in frames = virtual ms; NPC objecttype 6, player 0, mob 5 (`clif_bl_type`).
- Menu text is the options joined with `:` plus a trailing `:`; each `say` is followed by ZC_WAIT_DIALOG; the end of a
  script sends ZC_CLOSE_DIALOG; cancel (255 or CZ_CLOSE_DIALOG) cancels the coroutine.
- A chat message without the `"<name> : "` prefix is dropped silently (legacy), not a disconnect.
- Last position is persisted on logout in mod namespace `sim.world`, key `pos`, and restored on the next login of that account.
- A mod's `unit:before_move` cancel vetoes the step (engine-core's documented veto); hook context value is the unit's gid.
- Scenario sandbox budget: 200 M steps per slice, no wall limit.

## Not done

- A native `ctx` for the pipeline (waiting for script-host to expose the accumulator methods), skills, statuses, mobs.
- Replay of `GoldenTraceRecord` inputs beyond `damage`, `race`, `size`, `seed`, `golden=<id>`.
- Per-hook draw counters (script-host does not expose them); per-hook timing.
- Write-behind/WAL in the sim, multiple maps, instances, a second client build.
- Windows (`mods` and script-host need `__int128`; `popen`/`system` are used by the coverage and golden-expansion paths).
