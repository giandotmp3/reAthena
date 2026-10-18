# native-defaults

Opt-in plug-in of named, versioned native stages for the rathena-engine
(see `analysis/rathena-engine/REIMAGINED_ARCHITECTURE.md` sections 1.1, 4, 6.4).

- Stack: C++20 static library, CMake. No third-party dependencies.
- Not linked by the generic core. A ruleset opts in by stage ID, for example
  `native.damage.defense_soft@1`, supplying all numbers as data tables.
- Each native stage has a pure-Luau reference under `luau/reference/` and a
  differential test. A stage that any script hook touches runs the Luau reference.
- Native damage stages are built only if M0(a) shows pure Luau misses the tick
  budget. `native.mob_ai.default@1` is always native (BR-RULESET-113).

Status: domain model, registry, numeric helpers and stage ID parsing are
implemented. Damage stages and mob AI are stubs that throw `NotImplementedError`.
Their acceptance tests are expected-failure, tagged with the rule ID. An
expected-failure test that starts passing is reported as XPASS and fails the
run, so the marker must be removed when the stage lands.

Build and test:

    cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure

Config: `config/native-defaults.example.toml` holds no secrets. Nothing here
needs a database; if one is ever needed, read `${DATABASE_URL}` from the environment.
