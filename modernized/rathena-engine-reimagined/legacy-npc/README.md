# legacy-npc

Compatibility layer for rAthena `.txt` NPC scripts (REIMAGINED_ARCHITECTURE section 7, CAP-E08): the M0 interpreter prototype
(`tooling/m0/legacy_interp`, matches the real legacy script VM on 2,482 of 2,485 replay runs) moved into this unit as tested code, mapped
onto script-host. Package id `legacy.npc` (`mod.toml`, no permissions requested).

## Layout

| Path | What |
|---|---|
| `luau/` | The package. `parser`, `lower`, `interp`, `cfmt`, `commands`, `commands_ext`, `declsplit`, `driver`, `world` are the prototype modules (behaviour kept; changes are listed below). New: `vars` + `storage_mem` (sigil scopes on script-host storage), `loader` (lazy corpus), `runtime` (NPC runs, events, queue, timers), `commands_rt` (runtime commands), `blocking` (dialogue adapters), `events`, `report`, `dump`/`synth` (C++ facade protocol), `legacy` (entry), generated data `builtins`, `consts`. |
| `luau/tests/` | Luau test suites (run by the C++ runner inside a script-host sandbox, and by the Luau CLI): `t_semantics` (ported 1:1 from the prototype), `t_neg`, `t_vars`, `t_loader`, `t_events`, `t_runtime`. |
| `include/`, `src/` | C++ facade: declaration/command/event model, `parse_file` (runs the Luau parser in the sandbox), `Interpreter` (a script-host dialogue coroutine), the DB-script compiler, `analyze_file`, the AST cache, `PackageHost` (bundles `luau/` into one chunk and hosts it). |
| `tests/` | C++ acceptance tests (`test_*.cpp`), the Luau-suite bridge, the replay regression (`replay_regression.py` + `replay_glue/`), the corpus memory test (`memory_test.py`). |
| `tools/` | `gen_corpus_modules.py` (wrap a load set as Luau data modules), `corpus_bench.luau`, `unsupported_report.py` (the per-NPC unsupported-command table). |

## Build and test

    export RATHENA_LUAU_ROOT=/path/to/luau/src RATHENA_LUAU_LIB_DIR=/path/to/luau/build      # as in script-host/README.md
    cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
    build/legacy_npc_tests --list-pending          # expected failures (none left)
    build/legacy_npc_tests --print-commands        # regenerates src/supported_commands.inc from the Luau tables
    luau luau/tests/run_cli.luau                   # the Luau suites alone, no C++ (from luau/)

Without Luau the build still succeeds and every test that needs the VM reports `SKIP` (never pass). The output ends with
`passed=P expected-failures=X failed=F (unexpected-passes=U) skipped=S`; an expected failure that starts passing fails the run.

Two ctest entries wrap external artifacts and skip (exit 77) unless pointed at them:

    LNPC_REPLAY_BUILD=<replay build> LNPC_REPLAY_BUILD_PRE=<pre-renewal replay build> LNPC_LUAU_CLI=<luau>  ctest -R replay
    LNPC_LUAU_CLI=<luau> [LNPC_REPO=<checkout>]                                                              ctest -R memory

* `tests/replay_regression.py` runs `tooling/m0/replay/smoke.py` (unmodified) with `--interp` pointing at an assembled copy of `luau/` +
  `tests/replay_glue/` and requires the recorded baseline: renewal 15/15, pre-renewal 3/3, synthetic 16/16 strict (ordered host calls with
  arguments and returns, plus end state, against the real rAthena VM). `--corpus wide|hunt|synthetic` runs the larger corpora.
* `tests/memory_test.py` indexes the renewal load set and compares the lazy loader with the eager prototype pipeline.
* `python3 -I tools/unsupported_report.py --repo <checkout> --luau <luau> [--set re|pre]` prints the report.

## What changed relative to the prototype (everything else is byte-for-byte the replay-validated code)

* `interp.luau`: persistent registers are proxies (write-behind, flushed when a slice ends or blocks), `setvarIn` assigns arrays back,
  key/value limits, gated commands (`GATED`), `st.block` (dialogue adapters), `run` (no private coroutine, needed because script-host's
  `say/choose/input/wait` are valid only on the dialogue coroutine itself), `debug` optional, `MAX_DEPTH` 2048 to 32768.
* `world.luau`: registers come from a `vars` backend (default: in-memory storage with the script-host surface, so tests and replay
  exercise the real mapping). `commands.luau`: `getvariableofnpc(.v, "NPC")` as a value now reads that NPC (the prototype ignored the name);
  `rec` also feeds the optional structured trace sink. `lower.luau`: analysis side tables are off unless `lower.collect`.
  `declsplit.luau`: `noBody`/`all` options and `line`. `driver.luau`: forwards `granted`.
* `freeloop` is denied unless `unbounded_loop` is granted (the prototype allowed it); the replay glue grants all four gated commands because the
  legacy VM has no permission model.

## Variables (architecture section 7)

| Legacy | Scope here | Backing | Storage key (mod namespace `legacy.npc`) |
|---|---|---|---|
| `name` | character | `storage.character(char)` | `legacy:char/<name>` |
| `@name` | character temp | plain table per player (gone at logout) | |
| `$name` | world | `storage.world()` | `legacy:world/<name>` |
| `$@name` | world temp | plain table | |
| `#name` | account | `storage.account(acc)`, needs `storage.account` | `legacy:account/<name>` |
| `##name` | global account | same scope as `#`, distinct keys | `legacy:account_global/<name>` |
| `.name` | NPC object | table owned by the loaded NPC body, shared by duplicate clones | |
| `.@name` | call frame | Luau table per heap frame | |
| `'name` | instance | table per instance (writes refused outside an instance, as legacy) | |

The key spelling equals the C++ `storage_key()`. Arrays are one table value per name; storing `0` or `""` deletes the element, and the key
when nothing is left. Limits follow `script_check_RegistryVariableLength`: a name of 33 or more characters (sigil and `$` included) is refused
(`script.cpp:3144`, `:3224`: the write is skipped, the script goes on); a string value of 255 or more characters is refused for character and
account variables (`pc.cpp:11655`) and cut to 254 for `$` (the 254-character save column, `intif.cpp:459`); temporary scopes are unlimited.
`#`/`##` without the permission raise a typed `PERMISSION_DENIED` (`storage.account`); nothing else is affected.

**Q-H answered: `.` variables ARE shared by `duplicate()` clones.** They live in `script_code::local` (`script.cpp:2658`, read and written through
`st->script->local` at `script.cpp:2734`, `:2800`, `:3054`, `:3163`, `:3257`), and `npc_parse_duplicate` makes the clone use the template's code
object (`npc.cpp:4602`, `nd->u.scr.script = dnd->u.scr.script`). `Loader:varsOf` hands the same table to the template and every clone, and the clones
also share the parsed program. Tests: `LNPC-VAR-008` (C++ and Luau).

## Events, queue, timers

Every `On*` label of every NPC (and of every clone, as `npc_parse_duplicate` exports them per clone) registers one hook on the engine bus:
event `legacy:<Label>` with the label spelled as in the legacy configuration (`OnInit`, `OnTouch`, `OnTimer5000`, `OnClock0830`, `OnMinute30`,
`OnPCLoginEvent`, `OnMyMobDead`, ...), hook id `<npc unique name>::<Label>`, phase `on`, mode `observe`. The engine puts the attached
player id in the numeric context (`ctx:get()`, 0 = none). `Runtime:registerEvents(events)` runs while the mod chunk loads (`events.on` is load-time
only). The label table is `script.cpp:262-312`; the spec's "section 5 table" of event names does not exist in the documents of this repository
(only the category row of AI_NATIVE_SPEC_ENGINE 4.3), so the legacy configuration is the table.

Engine seams that do not exist yet are plain methods the engine (or a fake) calls: `rt:fireAll(label, player)` (engine-fired broadcast events;
`rt:init()` is `OnInit`), `rt:clockTick(now, prev)` (the `OnMinute/OnClock/OnHour/OnDay/OnSun..` family exactly as `npc_event_do_clock`),
`rt:fireEvent("Npc::OnLabel", player)`, `rt:player(rid, fields)` / `rt:dropPlayer(rid)`, `rt:setClock(ms)` / `rt:advance(ms)` (internal scheduler used
when the host has no `timers`). Unit tests drive them with a fake bus and the internal scheduler; `LNPC-EVT-004` and `LNPC-PKG-006` use the real
`ReloadManager` + `EventBus` + `timers.after`.

**Q-J answered (npc.cpp):** an event aimed at a player that is inside a script is queued in `sd->eventqueue[2]` (`:1751-1756`, `:5110-5114`; `MAX_EVENTQUEUE`
2, `map.hpp:73`). When both slots are taken the NEW event is dropped with a warning (`:1758-1759`, `:5116-5117`); nothing older is displaced. When
the player's script ends `npc_event_dequeue` (`:1176-1204`, called at `script.cpp:4331`) turns slot 0 into a 100 ms player timer
(`pc_addeventtimer(sd, 100, ...)`, `:1196`) and shifts slot 1 down; that timer calls `npc_event` again, so a still-busy player queues it again. This is
what `PlayerEventQueue` (C++) and `events.newQueue` (Luau) do, behind `compat.two_slot_event_queue` (default on; off = unbounded, nothing dropped).

NPC timers (`initnpctimer`, `startnpctimer`, `stopnpctimer`, `getnpctimer`, `setnpctimer`, `attachnpctimer`, `detachnpctimer`), player timers (`addtimer`,
`deltimer`, `addtimercount`, at most 32 per player), `donpcevent`, `doevent`, `enablenpc`, `disablenpc` follow `npc.cpp`/`script.cpp`; they are scheduled with the
script-host `timers.after` (owned by mod and generation: a reload drops them, tested in `LNPC-PKG-006`).

Event handlers started from the bus or a timer run non-interactively: `sleep` continues on the timer, a command that needs a client
(`next`, `menu`, `select`, `input`, ...) ends that run with a typed `needs_dialogue` outcome. An interactive run is `rt:run(npc, rid, label)` called
from a `DialogueManager::start` source.

## Blocking commands

`mes` text is buffered and leaves with the next dialogue request: `next`, `close`, `close2` -> `say(text)`; `menu`/`select`/`prompt` -> `say(text)` (if any)
then `choose(options)`; `input` -> `input(prompt)`; `sleep`, `sleep2`, `progressbar`, `progressbar_npc` -> `wait(ms)`. A disconnect cancels the coroutine
(`on_disconnect`); persistent variables are written when each blocking command starts, so nothing written before the block is lost.

## Safety and degradation

Command budget 65535 and goto budget 2048 per slice (`RunLimits`, `check_cmdcount`/`check_gotocount`) abort the script with `BUDGET_EXCEEDED`, not the tick.
`atcommand`, `query_sql`, `setunitdata`, `freeloop` need `atcommand`, `query_sql`, `setunitdata`, `unbounded_loop` in the runtime's `granted` list (mirror of
`mods.lock`; none by default) and raise `PERMISSION_DENIED` before the command is looked up. The effect of the first three is a host hook
(`cfg.host.hooks`), without one the command fails like a command that found nothing to do. An unimplemented command raises `UNSUPPORTED` naming it; the runtime
checks a body statically first (including `callfunc` targets) and refuses to start an NPC that uses one, so a disabled NPC has no partial side effects.
The report (`tools/unsupported_report.py`, `Runtime:report()`, `report.luau`) lists file, line, NPC, unsupported and gated commands.

## Lazy loading and size

`Loader:addFile` indexes a file (declaration splitter without bodies plus a text scan for event labels); a body is parsed and lowered on first use and only the
flat ops are kept; duplicates share one program table. Over the renewal load set (835 files, 19,717 NPC entries, 9,368 bodies, 23.0 MB of text, measured
as `collectgarbage("count")` deltas with all texts resident, `tests/memory_test.py`): eager prototype pipeline 320.1 MB, lazy index 16.4 MB (0.9 s), after 200
random NPCs were used 23.1 MB. The index scan finds exactly the labels the parser finds in all 13,605 script NPCs of both sets. Inside a script-host sandbox (limiting allocator, text retained)
indexing the renewal set grows the mod's memory by 52 MB in 2 s (`LNPC-LAZY-010`), so a mod that loads the corpus needs a memory cap above the 32 MB
default; registering the 27,017 event hooks of the renewal set on the real bus, with load and validation, takes about 7 s (`LNPC-EVT-013`). Persistent
variables count against the storage quota of the mod (default 1 MiB, `storage.default_quota_bytes`).

## Not done

* The flat-op cache (`cache.luau`, `Loader.new{ cache = {get, put} }`) is opt-in and nothing persists it yet: blobs are 8 KB per program on average (77 MB for the
  renewal set) and decode about 2.5x faster than parse + lower, so it is a restart optimisation, not a memory one. The cache key space is per load set: invalidate it
  when a `function script` changes, because a body's parse depends on the set of function names.
* `attachrid`/`detachrid` and the other multi-player command families, instances (the `'` scope works once the instance layer sets `env.instanceId`; there is no instance
  lifecycle and the `instance_*` commands fail outside one), `OnTouch` area tests (engine geometry), `unloadnpc`, `sleep` precision beyond the timer tick.
* Event handlers run by the bus or a timer are non-interactive (see above); an interactive run for them is started by the engine through `DialogueManager::start`.
  The engine must call `rt:endDialogue(rid)` when it cancels a dialogue (disconnect, idle timeout, reload drain): none of this package's code runs after a cancel.
* `granted` (`mods.lock` grants) is handed to `LEGACY.new` by the host; there is no Luau API to read the sandbox's own grants. `query_sql` and `setunitdata` have no
  entry in the mods unit's permission vocabulary, so `mod.toml` declares them only under `[legacy] optional_permissions`.
* 1,606 renewal NPCs (of 19,660) and 381 pre-renewal NPCs (of 9,813) use commands outside the supported set and are disabled by the report rule
  (`tools/unsupported_report.py`); the most frequent are `achievementexists`, `jobcanentermap`, `achievementcomplete`, `consumeitem`, `setcart`.
* Luau type-checking: `luau-analyze` reports lints and a few nonstrict `TypeError`s in the ported prototype modules (kept as they are); the new modules are clean.
