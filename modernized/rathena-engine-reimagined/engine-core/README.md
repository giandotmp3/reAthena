# engine-core

Rule-free C++20 simulation core of rathena-engine: generational ECS with
sparse-set pools, generic attribute aggregation, map grid and spatial index,
A* pathfinding, single-thread timers with a virtual clock, movement with
on-move hooks, and the instance primitive. It contains no skill, item, status,
job or formula vocabulary and no game numbers (lint test EC-LINT-001).

## Build and test

    cmake -S . -B build -G Ninja && cmake --build build
    ./build/engine_core_tests          # run
    ./build/engine_core_tests --list   # rule-tag coverage

Expected-failure tests (`PENDING_TEST`) encode the real expectation, must fail
until implemented, and fail the run if they unexpectedly pass (remove the
marker then). Use `-DENGINE_CORE_SANITIZE=ON` for ASan/UBSan.

## Rule tags

The spec assigns no numbered BR IDs to this service (BR-MAP/BR-RULESET rules
belong to rulesets, netproto, gateway). Tags `EC-*` are derived from spec 4.2
and architecture section 4: ECS, ATTR, GRID, DIST, PATH, TICK, MOVE, INST, LINT.

## Status

All primitives are implemented; there are no expected-failure tests left
(72 active tests). `tests/legacy_ref/` holds a trimmed copy of the legacy
A* / `path_search_long` / BHEAP code, compiled into the test binary only; the
differential tests (EC-PATH-010) compare the engine with it on thousands of
random maps. Not ported: the legacy "easy" direct path (`flag&1`), cell-stack
limits (`CELL_NOSTACK`), `BL_LIST_MAX`, anything that needs game state.

Decisions where the spec was silent (each read from legacy code, see the
comments in the headers and `src/path.cpp`):

- Timers: late-timer rule (callback receives `now` when more than 1000 ms
  late), interval re-arm (`scheduled + period`, or `now + period` after a stall
  over 1 s, catch-up firings inside one `run_due`), eager cancel, ids never
  reused, ties fire in insertion order (legacy heap is not stable).
- A*: neighbour order, binary-heap tie-breaks, and the fixed 32x32 node table
  whose slot collisions fail the search are reproduced bit for bit at the
  default config; `max_walkpath` also sizes the table.
- Lines: `LineBlock::Wall` (shots, area wall-check) vs `NoPass` (official
  walkpath rule); the last row/column block `NoPass` but not `Wall`.
- On-move hook: `before_move` Veto ends the walk without moving; `moved` may
  stop or retarget the walk.
- Instances: `ttl <= 0` means no expiry; `owner_left` destroys by exact owner.
