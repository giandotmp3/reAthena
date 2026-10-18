# tooling

Scaffold of the rathena-engine tooling service (C++20, CMake, no external dependencies).

Tools (each a thin CLI in `cli/` over the `rathena_tooling` library): `rathena-sim`, `rathena-migrate`,
`rathena-migrate-saves`, `rathena-pack`, `rathena-codegen`, `rathena-spike` (M0).

Implemented: virtual clock, deterministic RNG with draw counting, in-memory store, coverage report,
legacy per-field last-wins merge with Index patching, read-only legacy DB guard, reconciliation and
checksum helpers, credential-literal detection. Everything else throws `NotImplemented` and its
acceptance test is `PENDING_TEST` (expected failure; an unexpected pass fails the run so the marker
gets removed).

Rule ids: `BR-RULESET-115/119` come from the spec. The spec assigns no ids to the tooling behaviors in
architecture section 8, so `TL-*` ids are local and derived; see `rules/rule_manifest.tsv`.

Secrets: config uses `${ENV_VAR}` placeholders only (`config/tooling.example.toml`).

    cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
    build/tooling_tests --list
