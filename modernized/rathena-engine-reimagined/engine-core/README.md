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

Implemented: entity allocator, sparse sets, registry, attribute interning,
cell flags and edge quirk, distances, clock and poll clamp. Everything else is
a stub throwing `NotImplemented`.

Open questions from the spec (late-timer catch-up exact rules, hash-collision
behavior of the legacy search) are not asserted beyond what the spec states.
No configuration or credentials exist in this module.
