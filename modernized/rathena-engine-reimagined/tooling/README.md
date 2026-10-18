# tooling

Tooling service of the rathena-engine (C++20, CMake). The sim links the sibling units read-only (engine-core, script-host + Luau, mods, persistence, netproto) and needs the system SQLite and, at build time, Python 3 + PyYAML.

Tools (each a thin CLI in `cli/` over the `rathena_tooling` library): `rathena-sim`, `rathena-migrate`,
`rathena-migrate-saves`, `rathena-pack`, `rathena-codegen`, `rathena-spike` (M0).

Implemented: **`rathena-sim`** (see `sim/README.md`): the headless harness over engine-core, script-host (real Luau),
the mod loader, persistence (SQLite) and netproto framing, with SimPlayers that speak netproto frames, a Luau scenario
DSL, the BR-ID coverage report (`rathena-sim coverage`) and the conformance driver (`rathena-sim conformance`). Also
implemented: legacy per-field last-wins merge with Index patching, read-only legacy DB guard, reconciliation and
checksum helpers, credential-literal detection. Everything else throws `NotImplemented` and its acceptance test is
`PENDING_TEST` (expected failure; an unexpected pass fails the run so the marker gets removed). `m0/` and `migrate/`
are finished and not touched by the sim work.

**There is no gateway and no network in the sim.** It terminates netproto frames at a small world/session boundary of
its own; `sim/README.md` lists exactly what it exercises and what it does not.

Rule ids: `BR-RULESET-115/119` come from the spec. The spec assigns no ids to the tooling behaviors in
architecture section 8, so `TL-*` ids are local and derived; see `rules/rule_manifest.tsv`.

Secrets: config uses `${ENV_VAR}` placeholders only (`config/tooling.example.toml`).

    export RATHENA_LUAU_ROOT=/path/to/luau/src RATHENA_LUAU_LIB_DIR=/path/to/luau/build   # without Luau the sim tests SKIP
    cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
    build/tooling_tests --list
    build/rathena-sim run scenarios && build/rathena-sim coverage --suites && build/rathena-sim conformance --ruleset renewal
