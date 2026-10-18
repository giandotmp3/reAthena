# legacy-npc

Compatibility service for rAthena `.txt` NPC scripts (REIMAGINED_ARCHITECTURE section 7, CAP-E08).

Stack: C++20 (parser, AST cache, command registry, variable mapping, DB-script compiler)
plus a Luau-hosted interpreter (`luau/interpreter.luau`) run as an ordinary coroutine by `script-host`.

Status: scaffold. Domain model and API stubs exist; variable mapping, command registry/permission
gating, event-label classification and the 2-slot queue are implemented. Parser, interpreter,
DB-script compiler and unsupported report throw `NotImplemented`.

## Tests

    cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
    ./build/legacy_npc_tests --list-pending

Unimplemented acceptance tests are registered with `PENDING(rule_id, name, reason)`. They run
as expected failures; if one starts passing the runner fails ("XPASS") so the marker is removed.

## Rule IDs

The spec assigns no rule to this service by ID. Tests are tagged with:
- `BR-RULESET-098`, `BR-RULESET-112`: DB-embedded script compilation (bonus vocabulary, compiled deltas).
- `LNPC-*`: rule IDs derived from AI_NATIVE_SPEC section 4.3 and architecture section 7
  (variables, yields, events, safety, degradation). Pending confirmation of numbering by the spec owner.
- Open questions Q-H (`.` sharing across `duplicate`) and Q-J (queue drop contract) are tagged pending.

## Security defaults

- `atcommand`, `query_sql`, `setunitdata` and `freeloop` are denied unless the operator grants the permission in `mods.lock`.
- No credentials in config; SQL access is via `${DATABASE_URL}` owned by the persistence service.
