# mods

Mod loader and the mechanism-only `rathena.base` package (C++20, per
`REIMAGINED_ARCHITECTURE.md` sections 5, 6 and 10). **Implemented**; 124 acceptance
tests pass, none are pending.

Scope: TOML-subset parser, `mod.toml` manifest, dependency/ordering resolution with
cycle errors, `mods.lock` grants and optional signatures, namespaced id registry with
`client_id` handling and a tombstoned `id_map`, loader with kill switch and
observability counters, integration with script-host (`ReloadManager`, `ModSandbox`),
and the CI check that `base/rathena.base` references no ruleset-specific name.

## Build and test

    cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
    build/mods_tests            # prints "N passed, 0 failed, M expected-failures, K unexpected passes, S skipped"
    build/mods_tests --list     # rule-tag coverage report

The Luau VM is found through `RATHENA_LUAU_ROOT` / `RATHENA_LUAU_LIB_DIR` (see
`script-host/README.md`). Without it the core still builds and every test that needs
the VM reports `SKIP` (107 pass + 17 skip). `-DMODS_WITH_HOST=OFF` builds the core only
(103 tests). Warnings are errors (`-Wall -Wextra -Wpedantic -Werror`); for sanitizers
pass `-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined"
-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"` so script-host is instrumented
too. The core also builds warning-free with clang++ (script-host itself does not build
with clang + libstdc++: its `yaml_lite` uses an incomplete type in a vector).

Test conventions: `TEST` must pass; `PENDING_TEST` is expected-failure and an
unexpected pass fails the run (the macro is kept, no test uses it now); `SKIP_TEST`
reports a test that needs something absent from the build. Rule IDs:
`MOD-TOML|MANIFEST|DEP|PERM|ID|OPS|BASE|INT-nnn` (mods-service rules derived from
architecture 5/6; the only `BR-*` rule here is BR-RULESET-118, the ruleset manifest). The pipeline-runtime
integration tests (config table, protected stages, opt-in register) are in `tests/test_pipeline_host.cpp`.

`ctest` runs two tests: `acceptance` and `base_purity` (the CI gate, see below).

## Layout

| Path | What |
|---|---|
| `include/mods/toml.hpp`, `src/toml.cpp` | TOML subset parser and canonical writer |
| `manifest`, `semver`, `ids` | manifest model and validation, version ranges, namespaced ids |
| `resolver` | dependency graph, version solving, conflicts, exclusivity, load order |
| `lock`, `sha256` | `mods.lock`, grants, signature seam, SHA-256, package digest |
| `id_map` | id registry, store interface, in-memory store, range registry |
| `loader` | `ModLoader`: resolve, authorize, register ids, activate, kill switch, stats |
| `host_runtime` (`mods_host` library) | `ModActivator` on script-host: bundling, sandboxes, permissions |
| `base_check`, `tools/mods_tool.cpp` | forbidden-name derivation, purity check, CLI |
| `config/` | `id_ranges.example.toml`, `base_allowed_words.txt`, the original example name list |

`mods` (the core) has no dependency on script-host or persistence. `mods_host` links
both `mods` and `script_host`.

## TOML subset

`toml::parse` / `toml::write`. Supports comments, bare/quoted/dotted keys, `[tables]`,
`[[arrays of tables]]`, inline tables, arrays (multi-line, trailing comma, mixed),
basic and literal strings (single and multi-line, `\uXXXX`, `\UXXXXXXXX`), integers
(`_`, `0x`/`0o`/`0b`), floats (incl. `inf`, `nan`) and booleans. Rejected with
`file:line:column: message`: duplicate keys, redefined tables, extending an inline table
or a dotted-key table, unknown escapes, invalid UTF-8, control characters, bare CR,
integer overflow, date/time values (not needed by any manifest), nesting deeper than 64,
documents over 1 MiB. Columns count bytes. Values carry their line/column so the
manifest validator can point at the exact key.

Tests: a table of 20 malformed inputs with exact line, column and message, a valid
corpus, round trip (`parse(write(x)) == x`, and `write` is a fixed point) over **every
`.toml` in the engine tree** (12 files including the real manifests), a deterministic
mutation fuzz (30,000 mutants of the real files and the corpus, parse must succeed or
throw `ParseError` with a position, survivors must round-trip), and every prefix of
every real file. The third-party `3rdparty/**/*.toml` files also parse.

## Manifest (`mod.toml`)

Top-level keys: `id`, `version`, `engine_api`, `depends`, `conflicts`, `provides`,
`load_after`, `load_before`, `permissions`, `entry`, `description`, `name`, `license`,
`authors`, and the `[ruleset]` table. **Any other top-level scalar or array is an error**
(typo protection); **any other table is kept verbatim in `Manifest::config`** and never
interpreted (legacy-npc's `[legacy]`).

* `id`: `[a-z0-9][a-z0-9._-]*`, at most 64 bytes, no empty `.` segment.
* `version`: `MAJOR.MINOR.PATCH` (no pre-release/build suffix, no leading zeros).
* `engine_api`, `depends` values: version ranges (below). `depends` may be the inline
  table or a `[depends]` table; keys are mod ids; a mod cannot depend on or conflict
  with itself, and a dependency cannot also be a conflict. `load_after` and
  `load_before` may not overlap.
* `permissions`: strings from the closed vocabulary below (script-host's `Permission`
  enum). Unknown strings and duplicates are errors. **`sql.read` is rejected** with a
  message that points at architecture section 5.

  | String | script-host `Permission` | Enforced end to end |
  |---|---|---|
  | `storage.account` | `StorageAccount` | yes |
  | `storage.read:<mod>` | `StorageReadOther` + per-target `grant_read` | yes |
  | `unbounded_loop` | `UnboundedLoop` (lifts the step limit) | yes |
  | `pipeline.protected_stages` | `ProtectedStages` | yes (below) |
  | `http.route`, `atcommand`, `net.raw` | `HttpRoute`, `Atcommand`, `NetRaw` | no host API yet |

  **`pipeline.protected_stages`**: replace, cancel or move a pipeline stage that its
  defining ruleset registered `protected` (both rulesets: `init` and `final` of
  `weapon`), or cancel a run that would skip one. Inserting stages next to a protected
  stage needs nothing. Take-away for the PRE -> RE delta: its 23 operations (3 cancel,
  12 insert_after, 8 replace, `ruleset-renewal/docs/pipeline_diff.json`) include
  `replace('init')`, so a package that applies them must request this permission.
  Grant semantics are those of every permission (see `mods.lock`): not requested means
  not in force even when granted; requested and not granted stops the whole generation
  (`permission.ungranted`) before any code runs. A mod that does not request it and
  edits a protected stage is refused by script-host's reload with `cannot <replace|cancel|move>
  protected stage weapon/<stage> without the protected-stage permission`.
* `entry`: either the array form `entry = ["scripts/main.luau"]` (relative `.luau` module
  paths run at load, see Host integration) or the table form

      [entry]
      modules = ["scripts/init.luau"]   # optional; same rules as the array
      register = true                   # optional, default false

  `register` is the opt-in for `register(engine)` (see Host integration). Any other key
  in `[entry]` is an error (`entry.<key>`); `entry` cannot be both forms.
* `[ruleset]` (needs `provides = ["ruleset"]`): `mode` (`renewal`, `prerenewal` or
  `pre-renewal`), `db_set` (`re`, `pre-re`, `db/re`, `db/pre-re`, consistent with
  `mode`), the sub-flags `cast drop exp lvdmg aspd stat edp` as booleans in `flags`
  **or** `subflags` (the shipped manifest uses `subflags`, the test uses `flags`; both
  at once is an error), `legacy_quirks`, `strict_parity`.

Errors have a stable code (`manifest.syntax|missing_key|unknown_key|bad_type|bad_id|
bad_version|bad_range|bad_permission|bad_value|duplicate|inconsistent|io`) and read
`mod 'ruleset.x' (path/mod.toml:12:5): key 'permissions[1]': <reason>`.

### Version ranges

Terms separated by whitespace and/or commas, all must hold: `^1.2.3` (>=1.2.3 <2),
`^0.1` (>=0.1.0 <0.2), `^0.0.3` (exactly), `^0` (<1), `~1.2.3` (>=1.2.3 <1.3), `~1`,
`>=`, `>`, `<=`, `<`, `=`, bare `1.2.3` (exact), bare `1.2` / `1` (the minor / major
line), `x`/`*` wildcards, empty = any, a space after an operator is fine. No
pre-releases. A malformed range never matches (and the manifest validator rejects it).
The shipped `ruleset-prerenewal` uses `">=0.1, <1"`, which is why the comma form is
accepted.

## Resolution

`resolve_load_order(manifests, options)` returns the manifests in a deterministic
order: dependencies first, then `load_after`/`load_before` (an edge naming an absent
mod is ignored: optional ordering), ties broken by id (Kahn with a sorted ready set, so
the result does not depend on input order; a 300-case random-graph property test checks
this and every constraint). Checks, in this order, all naming the mods involved:
duplicate id, `engine_api` against `ResolveOptions::engine_api_version`, missing
dependency, version mismatch, declared conflict (a `conflicts` entry may name a mod id
or a capability another mod `provides`), exclusive capabilities (`exclusive_provides`,
default `{"ruleset"}`: more than one provider is an error), required capabilities
(`required_provides`, default empty; the server sets `{"ruleset"}` so exactly one ruleset
is enforced), and cycles. A cycle message lists the full path and why each edge exists:

    load order cycle: a -> c -> b -> a (each mod must load before the next:
    'c' depends on 'a'; 'b' load_after 'c'; 'a' depends on 'b')

`select_versions(available, roots)` is the version solver for a repository with several
versions per mod: highest satisfying version per mod, backtracking on dead ends, and
the failure message names the requiring mod and the range.

## `mods.lock`

    lock_version = 1
    require_signatures = false
    trusted_key_hash = "sha256:<64 hex>"
    [mod."example.mod"]
    grants = ["storage.account"]
    digest = "sha256:<64 hex>"          # optional content pin
    signature = "..."                    # optional

Nothing is granted unless listed. `effective_grants` = requested AND granted (a grant for
something the manifest did not request is dropped: least privilege; e.g. `grants =
["pipeline.protected_stages"]` on a mod that does not request it changes nothing). `authorize` throws
`signature.missing|signature.invalid` (a present signature is always verified, the
policy only makes it mandatory), `digest.mismatch`, `permission.ungranted` (names the
mod and every missing permission). `write_mods_lock` produces canonical text.

**What is and is not cryptographic.** SHA-256 is implemented here and checked against
the FIPS vectors (including the million-`a` message). `package_digest(dir)` is a real
content hash of a package. The signature check goes through the injected
`SignatureVerifier`; the built-in `make_keyed_hash_verifier` is a keyed SHA-256 MAC over
`(mod id, package digest)` with a shared secret from the environment
(`RATHENA_MODS_SIGNING_KEY`, never a config file) plus a check that
`sha256(key) == trusted_key_hash`. It detects tampering by anyone without the key and
pins which key is in force. **It is not a public-key signature**: whoever can verify can
also sign. The seam exists so an Ed25519 verifier can replace it. `mods_tool sign`
produces signatures for operators. All values in tests are fake.

## Id registry and `id_map`

Ids are `mod:kind/name` (`NamespacedId`, matches the migrator output, e.g.
`ruleset.prerenewal:skill/sm_bash`). `IdMap`:

* `claim(id, client_id)`: explicit number (migrated legacy content). Idempotent for the
  same pair. A number taken by another live id is `id.collision` naming both mods and
  both ids; a tombstoned number is `id.collision` naming the former owner;
  re-declaring a live id with another number is `id.client_id_changed`.
* `allocate(id)`: lowest free number in the kind's reserved range, idempotent per id,
  `id.exhausted` when full, `id.no_range` for a kind without a range (30,000
  allocations run in milliseconds).
* `tombstone_mod(mod)`: its numbers become tombstones, never reassigned or claimable.
  Removal is explicit; the loader never tombstones a mod just because it is absent from
  a generation.
* Persistence seam, for the persistence unit to implement (no dependency on its code):

      class IdMapStore { virtual std::vector<IdMapRow> load() = 0;
                         virtual void save(const std::vector<IdMapRow>&) = 0; };
      // row: kind TEXT, client_id INTEGER, id TEXT NULL (NULL = tombstone), was_id TEXT NULL
      // key (kind, client_id, id); a tombstone is the only row of its number

  `MemoryIdMapStore` is the in-memory implementation. `load` rejects corrupt tables
  (`id_map.corrupt`).
* **Mutually exclusive mods may share explicit numbers** (`set_exclusivity`, fed by the
  loader from `conflicts`). Without this the two rulesets, which keep the legacy numbers
  by design, could never both appear in one id_map. The wire lookup then takes the
  active mod set: `id_of(kind, number, active_mods)`; the 2-argument form returns
  nothing for a shared number.
* `ModLoader` registers a generation's `DefinitionDecl`s on a scratch copy of the map,
  explicit claims first and allocations second, and commits the copy only if the whole
  generation succeeded.

`RangeRegistry` (`config/id_ranges.example.toml`) is the CI range check: owners'
explicit ranges per kind plus the allocation range per kind. `validate(known_mods)`
fails on overlap unless the two owners are mutually exclusive, and on an allocation
range overlapping an owned one; `check_claim` fails a number outside its owner's ranges.
The integration test checks every shipped `ruleset-prerenewal` definition against it.

## Loader and host integration

`ModLoader::load(request)` is all-or-nothing: resolve, authorize every mod (signature,
pin, permissions) before any code runs, register ids on a scratch map, activate through
a `ModActivator` session, then commit. On any error the previous generation (state, ids,
sandboxes) stays active and the error is rethrown; the observer gets a `Refused` event
with the precise text.

`HostRuntime` (in `mods_host`) is the `ModActivator` over script-host:

* One `ModSandbox` per mod via `ReloadManager`. `mods.lock` grants become the
  sandbox's `granted` set (`to_host_permission`; `storage.read:<mod>` also calls
  `grant_read`), so an ungranted API is refused by the sandbox with a typed
  `PermissionDenied` error: `[none.mod] a: permission denied: account storage requires
  the storage.account permission`.
* A generation is a fresh `ReloadManager` + `StorageApi` swapped in on success. This is
  the way to get all-or-nothing across mods: script-host has no unload and no
  cross-mod two-phase commit. Per-mod hot reload of the live world is `reload_mod`
  (script-host generations). Storage content survives through the shared backend;
  grants are rebuilt per generation (script-host cannot revoke).
* **`config` table.** For a mod with a `[ruleset]` table the activator fills
  `ModEnvironmentSpec::config`, which script-host exposes as the read-only global
  `config` (and `engine.config`). Keys (flat, values are booleans or strings):
  `mode` (as written: `renewal`, `prerenewal` or `pre-renewal`), `db_set` (when set),
  `renewal` (bool), `legacy_quirks` and `strict_parity` (only when the manifest sets
  them; script-host defaults `legacy_quirks` to true when absent), and one
  `subflag_<name>` (`subflag_cast`, `subflag_drop`, `subflag_exp`, `subflag_lvdmg`,
  `subflag_aspd`, `subflag_stat`, `subflag_edp`) per sub-flag the manifest lists. Mods
  without `[ruleset]` get only script-host's default (`legacy_quirks = true`); other
  manifest tables (`Manifest::config`, e.g. legacy-npc's `[legacy]`) are not copied.
  The ruleset packages read `config.legacy_quirks` when their entry module loads, so
  flipping it in `mod.toml` and reloading the generation switches the defect
  reproduction (renewal golden case 2520: 1072 with `true`, 740 with `false`).
* Bundling: `bundle_package(dir, opt)` turns `scripts/`, `data/` and `luau/` (not `tests`,
  `build`, `conformance`, `tools`, `types`) into one chunk with `require` and `import`
  shims (the sandbox has neither). Entries: manifest `entry`, else `scripts/init`, else
  every module.
* **The entry/register rule.** The call site is `bundle_package` in
  `src/host_runtime.cpp` (the loop over `__entries` at the end of the chunk). It calls
  `register(engine)` on an entry module only when the mod opted in with
  `[entry] register = true` (`BundleOptions::register_entries`); the same flag makes the
  host define the global `engine` table for that mod (`ModEnvironmentSpec::expose_engine`).
  Without the flag nothing is called and `engine` is nil, so `rathena.base` (no `entry`,
  so every module is an entry, and `bonus.register(name, handler)` means something else)
  is untouched. The ruleset packages do not opt in: they register themselves from
  their entry module with the always-present globals `pipeline`, `num`, `config`
  (setting the flag on them would register twice and fail with a duplicate `define`).
  `HostConfig::bundle_for[id]` replaces both the module list and the flag.
* Kill switch: `ModLoader::disable(id)` (`mod disable <id>`) disables the mod and its
  hard dependents; the host removes their hooks from the bus and reloads an empty chunk
  (drops timers, frees the state). If script-host refuses the empty reload because the
  mod declared a definition shape, hooks are still removed and the note says timers stay
  until restart. Operator kill switches survive reloads; disabled mods are prepared but
  their code is not run; `enable` re-activates (generation + 1 for that mod).
* Observability, `ModLoader::stats(id)`: load time of the mod's activation, memory used
  and peak (the sandbox allocator), hook calls, hook errors, hook time. **Hook time is
  attributed from whole-dispatch timing split by how many hooks each mod ran**;
  script-host has no per-hook timer, so it is exact only when one mod ran. Real figures
  on this machine (debug build): `legacy.npc` 75 ms / 5.6 MB, `rathena.base` 0.6 ms /
  0.57 MB, `ruleset.prerenewal` 4 ms / 0.97 MB.
* Not wired: atcommand/http.route/net.raw have no host API yet, so only
  `storage.account`, `storage.read:<mod>`, `unbounded_loop` and `pipeline.protected_stages`
  are enforced end to end by tests.

## `rathena.base` purity check

`mods_tool check-base --base mods/base/rathena.base --ruleset <pkg>... --migrate-out
<dir>... --allow config/base_allowed_words.txt` derives the forbidden list, matches
whole tokens case-insensitively (`SM_BASH` and `sm_bash`, full ids, package names; a
token is a run of `[A-Za-z0-9_]`, names with `.`/`:`/`/` must match exactly) in every
file under base, and exits 1 on any hit, 2 if the derivation is empty or mis-pointed
(it never passes silently with an empty list).

Derived from: the ruleset packages' mod ids and definition ids (literal
`"mod:kind/name"` strings and `local P = "mod:kind/"` ... `P .. "name"`, 18 ids today)
and the migrator output keys (`["mod:kind/slug"] = {`, 63,403 ids in `out/prerenewal`
and `out/renewal`; generated and git-ignored, so used when present). Each id contributes
the full id, the mod id and the slug (slugs shorter than 4 characters are skipped), 109,086
names in total. One real false positive exists: the status slug `freeze` is Luau's
`table.freeze`; it is in `config/base_allowed_words.txt` with the reason, and the test
fails if an allow entry stops suppressing anything. The shipped base passes. Planted
references (upper-case constant, full id, package name, a comment) are caught.

## Findings and decisions

Findings (things in the repo that are inconsistent, not fixed because they are outside
this unit):

1. **Architecture 6.1 lists `sql.read`**; section 5 says no general SQL permission
   exists. This unit follows section 5. The 6.1 line should read: "`permissions` (e.g.
   `storage.account`, `storage.read:other`, `http.route`, `unbounded_loop`,
   `atcommand`, `net.raw`; there is no general `sql.read`, see section 5)".
2. **`ruleset-prerenewal/mod.toml` puts `permissions = []` inside `[depends]`**
   (it follows the table header, so TOML scopes it to `depends`). The loader lifts
   array-valued manifest keys found in `[depends]` to the top level and records a
   `Manifest::warnings` entry; the fix is to move the line above `[depends]`.
3. **No engine version satisfies the shipped manifests**: `ruleset-prerenewal` says
   `engine_api = ">=0.1, <1"` while `legacy-npc`, `ruleset-renewal` and `rathena.base`
   say `"^1"`. `MOD-INT-001` asserts the precise refusal; the combined tests use an
   in-memory copy with `engine_api = "^1"`.
4. `rathena.base` is `0.1.0` and the rulesets depend on `"^0.1"` (CLAUDE.md item 9); a
   `"^1"` dependency, as architecture 6.1 writes it, is refused (`MOD-INT-002`).
5. `ruleset-renewal` keeps its mode/db_set/sub-flags in `package.toml`, not in a
   `[ruleset]` table of `mod.toml`; the loader accepts both manifests but does not read
   `package.toml`.
6. ~~`ruleset-prerenewal/scripts/init.luau` expects a global `engine`~~ resolved: both
   rulesets register themselves from the globals `pipeline`, `num`, `config`; `engine` is
   opt-in per mod (`[entry] register = true`).

Decisions where the spec was silent: the protected-stage permission is spelled `pipeline.protected_stages`; register(engine) is opt-in through `[entry] register` (default off); `config` keys are flat (`subflag_<name>`, `renewal`) because script-host's `ConfigValue` is a scalar; unknown top-level tables are kept, other unknown
keys are errors; `depends` names mod ids only; load order ties break by id; a signature
that is present is verified even when the policy is off; `lock.entries` for mods that
are not requested are dropped, not errors; a mod may declare ids only in its own
namespace (`id.unknown_mod` otherwise); mutually exclusive mods may share explicit
numbers; absence of a mod never tombstones; kill switch state survives reloads;
`required_provides` is opt-in; package digest = SHA-256 over sorted `path\0filehash`.

## Not done

* Ed25519 (or any public-key) verification; only the seam and the keyed-hash MAC.
* Persistence-backed `IdMapStore` (the persistence unit owns it); only the interface
  and an in-memory store.
* Per-hook timers and per-tick degradation (script-host does not expose them);
  `HostRuntime::dispatch` attributes time as described above.
* Extracting definitions from Luau automatically: `LoadRequest::definitions` is supplied
  by the caller (the test scans the ruleset data files with a regex).
* Reading renewal's `package.toml`, copying other manifest tables into `config`, and
  enforcing `http.route`, `atcommand` and `net.raw` (no such host APIs exist). No shipped
  package uses `[entry] register = true` yet; only synthetic mods exercise it.
* A `mod disable` command surface and a lock-file editing tool beyond `mods_tool`.
* Renewal is not loaded in the integration test beyond manifest resolution and id
  swapping (its Luau scripts are a Python-tested subset).
