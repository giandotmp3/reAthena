# netproto

Wire-protocol layer of rathena-engine (see `analysis/rathena-engine/REIMAGINED_ARCHITECTURE.md`
sections 2, 4, 8 and the spec section 4.1): packet spec, codegen, per-session codec sets,
framing, obfuscation, phase gating, anti-cheat framing rules, sim client, fuzz targets.
One client `(date, flavour)` is generated first: `re/20211103` (what the stock legacy build selects, see
"Version model") plus the hypothetical `main/20211103` the scaffold started with; the codec table is
keyed by version so more can be added as data.

Stack: C++20, CMake, no third-party dependencies in the C++ code. Codegen and the legacy tooling are
Python 3 (+ PyYAML for codegen); `g++` is only needed for the optional compiler cross-check.

## Layout

- `proto/client_packets.yaml` machine-readable spec: the semantic layer (names, direction, gate) over legacy data, verified against it
- `proto/legacy_20211103.yaml` GENERATED evaluation of the legacy packet DB for the default build (do not edit)
- `tools/legacy/` the evaluator: `cpp_subset.py` (subset C preprocessor), `extract_packets.py` (generator), `crosscheck_gxx.py` (g++ oracle)
- `tools/codegen/netproto_codegen.py` validate + resolve + emit frame tables and the C++ catalog + `--verify-legacy` (field codecs pending)
- `include/netproto/` domain model (`spec.hpp`), framing, gating, connect check, flood guard, codec set, obfuscation, sim client
- `fuzz/` libFuzzer targets (`-DNETPROTO_FUZZ=ON`, clang)
- `tests/` acceptance tests, tagged by rule ID (`test_legacy_spec.cpp` runs over the generated real catalog; `test_legacy_tools.py` tests the tooling)

## Rules owned

| Rule | Subject | Status |
|---|---|---|
| BR-MAP-059 | framing, phase gating, unknown opcode, 3 packets/cycle | implemented; obfuscated-opcode path pending |
| BR-MAP-060 | connect packet sanity checks | implemented; decode-from-wire path pending |
| BR-INTER-011 | connection flood guard | implemented |
| BR-INTER-012 | packet and buffer size limits | implemented |
| BR-INTER-013 | declared-length validation (S4) | implemented in framing; decoder-side check pending |
| CAP-E02 / CAP-24 | generated codecs, multi-version, obfuscation | model, resolve, negotiate implemented; decode/encode/obfuscation/sim client pending |
| SEC-S1 | no credential literals | implemented |

Dropped (architecture section 9): the hand-written inter-server length tables and handshakes.
Inter-server traffic is in-process now; BR-INTER-001..003, 007 belong to `gateway`/`persistence` seams.
Secure defaults chosen: an out-of-phase packet disconnects, an unsupported client version is refused
(no closest-version fallback), a 6th connection in a burst is rejected (legacy allowed 6).

## Build and test

    cmake -S . -B build -DNETPROTO_SANITIZE=ON && cmake --build build && ctest --test-dir build --output-on-failure
    ./build/netproto_tests --list     # rule-tag coverage

`ctest` runs three tests: `acceptance` (the C++ binary: `N passed, 0 failed, M expected-failures`),
`verify_legacy` (`netproto_codegen.py --verify-legacy`: re-evaluates the legacy headers and fails if
`proto/legacy_20211103.yaml` or any entry of `client_packets.yaml` differs) and `legacy_tools`
(28 Python tests: the preprocessor subset, determinism, negative tests, and the g++ cross-check,
which is skipped only when `g++` is absent). The legacy tree is found above the unit directory, or via
`RATHENA_LEGACY_ROOT`; with no legacy tree `verify_legacy` fails (it never passes vacuously).

`PENDING_TEST` is expected-failure: it must fail now; if it starts passing the run fails (XPASS)
so the marker is removed. Obfuscation keys are deployment data supplied through the environment
(`NETPROTO_KEYS_*`); tests use fake keys only.

## Version model and the legacy packet spec

### Where the numbers come from

`proto/legacy_20211103.yaml` is not typed by hand. `tools/legacy/extract_packets.py` evaluates, for one
`(PACKETVER, flavour)`, the same files in the same order as `packetdb_readdb()` in `src/map/clif.cpp`:
`clif_packetdb.hpp`, then `clif_shuffle.hpp`; a later live definition of an opcode overwrites an earlier
one; opcodes above `MAX_PACKET_DB` (0xCFF) are dropped. Conditions (`#if/#elif/#else`, `defined()`,
`PACKETVER`, `PACKETVER_MAIN/RE/ZERO_NUM`) are evaluated by `cpp_subset.py`, a small deterministic C
preprocessor subset (no network, clock or environment). Typed structs come from `packets_struct.hpp` and
`packets.hpp` (`DEFINE_PACKET_HEADER/ID`, `#pragma pack(1)` layout, `#if` fields resolved), and doc
names from the `/// 0436 ... (CZ_ENTER2)` comments above each `clif_parse_*` in `clif.cpp`.

`tools/legacy/crosscheck_gxx.py` checks the evaluation against a real compiler: `g++ -E` over a wrapper
that redefines `packetdb_addpacket` to a recording call, then compiles and runs it, so the compiler
evaluates every `#if`, `HEADER_*`, enum and `sizeof`; it also compares `sizeof/offsetof` of every laid-out
struct. Result for the default build: 1390 of 1390 live definitions identical (opcode, length, handler,
offsets, source line), 948 of 948 final opcodes, 527 struct sizes, 1956 field offsets, 474 header ids; 0
disagreements. The same holds for `main` forced (patched copy of the headers) and for PACKETVER
20100101, 20130515, 20180704, 20220330 and 20250122. A deliberately broken evaluator (`>=` read as `>`)
is detected (test `test_the_oracle_catches_a_broken_evaluator`).

`netproto_codegen.py --verify-legacy` re-runs the evaluation and fails if `legacy_20211103.yaml`
differs from it, or if any version entry of `client_packets.yaml` disagrees with the legacy table for any
target it applies to (opcode present, fixed/variable length and prefix offset, handler, `pos[]` offsets,
struct fields, listed aliases), or applies to no verified target at all.

### What the legacy table is (facts that changed the starter spec)

- **The default build is the RE flavour.** `src/config/packets.hpp` defines `PACKETVER_RE` for PACKETVER
  20200902..20211118, so the default 20211103 is `re/20211103`; `main/20211103` cannot be built from
  the legacy tree. It is kept as a *hypothetical* target (flavour macros forced), and differs in exactly
  one packet that matters (`WantToConnection`: 23 bytes RE, 19 main) plus 4 opcodes that exist only in RE.
- **`clif_shuffle.hpp` is live for this target.** Its last block (`PACKETVER > 20180307`) defines the
  opcodes of unshuffled modern clients (0x035f WalkToXY, 0x0360 TickSend, 0x0436 WantToConnection ...).
  Only that one of its 155 per-build blocks matches; the per-build `PACKETVER == X` blocks are for other
  dates.
- **The table is cumulative, not a snapshot.** Each `#if PACKETVER >= X` layer overwrites per opcode,
  never per handler, so old opcodes stay live: `WalkToXY` is routed from 0x00a7, 0x035f, 0x0881 and
  0x0953; `WantToConnection` from 0x009b, 0x0436, 0x086a, 0x0888, 0x094b; while the starter's 0x0072
  and 0x0085 are now `UseSkillToId` and `ChangeDir`. The scaffold names one canonical opcode per name
  (the one the legacy doc comment names, i.e. the last layer) and records the rest as
  `also_accepted_by_legacy`. They are not accepted here (unknown opcode disconnects); that is a
  deliberate divergence, listed as verified data for a later decision.
- **The DB has no names and no direction.** Semantic names come from the doc comments and struct
  names; direction is not in the DB (handler present = client-to-server). Server-to-client writers
  with a typed struct and a `HEADER_*` id that the DB does not list are kept in `struct_only_packets`
  (318 at this target, e.g. `ZC_NOTIFY_PLAYERMOVE` 0x0087, 12 bytes).
- **Starter values that were wrong:** `CZ_ENTER` was 19 bytes (23 on RE); chat was 0x008c (now 0x00f3);
  `CZ_REQUEST_MOVE` was 0x0085 (now 0x035f). `CA_LOGIN` 55 bytes at 0x0064 was right. The two
  `ZC_FEED_PET/MER` structs disagree with their `packet_db` length (7 vs 5); reported, not reconciled.

Counts at `re/20211103` (default build): 1390 live definitions (1363 in `clif_packetdb.hpp`, 27 in
`clif_shuffle.hpp`), 948 live opcodes, 400 with a handler (parseable), 283 variable-length (-1, length
word at offset 2), 117 opcodes whose earlier live definition is overwritten by a later one (a further view: 272 opcodes
have more than one definition line in the two files, live or not, for any target), 0 dropped as out of range, 161 with typed struct
fields, 786 without (handler-parsed or unhandled: `fields: unknown`, offsets only, no types guessed),
1 struct that could not be laid out (0x0166: an anonymous bitfield group that is not `uint8`).
`main` forced: 1386 definitions, 944 opcodes.

### Adding another target

A target is `(PACKETVER, flavour)`; flavour is derived by `src/config/packets.hpp` exactly as a build
would. To add one:

    python3 tools/legacy/extract_packets.py --legacy-root <repo> --packetver 20180704 --out proto/legacy_20180704.yaml
    python3 tools/legacy/crosscheck_gxx.py --legacy-root <repo> --packetver 20180704 --work-dir /tmp/x   # must report 0 disagreements

then add `{ id: "main/20180704", flavour: main, date: 20180704, obfuscation: { keys_env: ... } }` to
`client_versions`, add or extend `versions:` entries (their `applies` predicate decides which targets
they cover; every entry must match a listed target) and run `netproto_codegen.py --verify-legacy`, which
now also needs `legacy_20180704.yaml` to match. A forced flavour that no build selects needs
`--flavour main|re|zero` and is marked `hypothetical: true`; its oracle uses a patched copy of the headers.

### Not yet captured

- **Opcode obfuscation / the shuffle key.** `PACKET_OBFUSCATION` is on for this target (a rolling XOR of
  the opcode with a key derived from three per-deployment keys, `clif_cryptKey`). The keys are deployment
  data and the stateful decoder is still pending (`OpcodeObfuscator`). What *is* captured from
  `clif_shuffle.hpp` is only the block that is live for this target; the other per-build shuffle blocks
  are evaluated only when that target is generated.
- **Field layouts of handler-parsed packets.** 786 of 948 opcodes have only their `pos[]` offsets;
  types, names and widths live in `clif_parse_*` bodies and are not extracted (no guessing). The starter's
  `dest: pos3` for `CZ_REQUEST_MOVE` was dropped for that reason.
- **Packed sub-encodings.** The 3-byte x/y/dir and 6-byte src/dst forms appear in the structs only as
  `u8[3]`/`u8[6]`; `pos3`/`pos6` stay in the model but nothing in the legacy data ties a field to them.
- **Struct features beyond the subset:** unions and bitfield groups other than `uint8`, multi-dimensional
  arrays (reported as `unresolved`), `char` arrays are typed `string` with `width` (the DB does not say
  whether they are NUL-padded text).
- **Other semantics the legacy tables do not carry:** which opcodes are valid in which phase (gates here
  are the BR-MAP-059 rules, not extracted), message direction for unhandled opcodes, per-field presence
  conditions across versions (a single target resolves them).
- **Decode/encode, the sim client and the YAML loader in C++** (`load_catalog_yaml`) remain pending; the
  C++ side consumes the *generated* catalog (`generated/catalog_client_packets.hpp`).
