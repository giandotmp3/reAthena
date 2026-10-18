# mods

Mod loader and the mechanism-only `rathena.base` package (C++20, per
`REIMAGINED_ARCHITECTURE.md` sections 6 and 10).

Scope: `mod.toml` manifest, dependency/ordering resolution with cycle errors,
`mods.lock` grants and optional signatures, namespaced id registry with
`client_id` handling and tombstoned `id_map`, kill switch, observability hooks,
and the CI check that `base/rathena.base` references no ruleset-specific name.

Build and test:

    cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
    build/mods_tests --list     # rule-tag coverage report

Test conventions: `TEST` must pass; `PENDING_TEST` is expected-failure (stub
throws `NotImplemented`) and an unexpected pass fails the run so the marker is
removed. Rule IDs: `MOD-MANIFEST|DEP|PERM|ID|OPS|BASE-nnn` (mods-service
rules derived from architecture 5/6; no BR-* rule is assigned to this service
other than BR-RULESET-118, the ruleset manifest).

Security defaults: nothing is granted unless listed in `mods.lock`; unknown
permission strings are errors; signature verification keys come from the
environment, never from config; all digests/signatures in tests are fake.

Open: the architecture lists `sql.read` in `mod.toml` examples but also says no
general SQL permission exists. Scaffold follows the latter (not in vocabulary).
