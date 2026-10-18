# netproto

Wire-protocol layer of rathena-engine (see `analysis/rathena-engine/REIMAGINED_ARCHITECTURE.md`
sections 2, 4, 8 and the spec section 4.1): packet spec, codegen, per-session codec sets,
framing, obfuscation, phase gating, anti-cheat framing rules, sim client, fuzz targets.
One client `(date, flavour)` is generated first: `re/20211103` (what the stock legacy build selects, see
"Version model") plus the hypothetical `main/20211103` the scaffold started with; the codec table is
keyed by version so more can be added as data.

Stack: C++20, CMake, no third-party dependencies in the C++ code. Codegen and the legacy tooling are
Python 3 (+ PyYAML for codegen); `g++` is only needed for the compiler cross-checks (`legacy_tools`,
`layout_oracle`). The build itself needs Python 3 + PyYAML and nothing else.

## Layout

- `proto/client_packets.yaml` the curated semantic layer (names, direction, category, gate) over legacy data, verified against it; also the overlay the full catalog takes its names from
- `proto/packets_re_20211103.yaml` GENERATED full catalog (1186 packets) derived from the legacy sources (do not edit)
- `proto/legacy_20211103.yaml` GENERATED evaluation of the legacy packet DB for the default build (do not edit)
- `tools/legacy/` the evaluators: `cpp_subset.py` (subset C preprocessor), `extract_packets.py` (packet DB + struct layouts), `handler_layout.py` (mechanical `clif_parse_*` layouts), `derive_catalog.py` (the full catalog), `crosscheck_gxx.py` and `layout_oracle.py` (g++ oracles)
- `tools/codegen/netproto_codegen.py` validate + resolve + emit frame tables and the catalog row tables + `--verify-legacy` + `--verify-catalog`
- `include/netproto/` domain model (`spec.hpp`), YAML reader (`yaml.hpp`), codec set (decode/encode), framing, gating, connect check, flood guard, obfuscation, sim client
- `fuzz/` libFuzzer targets (`-DNETPROTO_FUZZ=ON`, clang) and the invariants they share with the deterministic fuzz test
- `tests/` acceptance tests, tagged by rule ID; `tests/golden/` byte vectors written by the compiled legacy structs; `tests/legacy_ref/` copies of the legacy routines (with licence notes) that print the expected vectors; `tests/compat/` the gateway / sim compatibility tests

## Rules owned

| Rule | Subject | Status |
|---|---|---|
| BR-MAP-059 | framing, phase gating, unknown opcode, 3 packets/cycle | implemented, including the obfuscated-opcode path (`peek_frame(..., obfuscator)`) |
| BR-MAP-060 | connect packet sanity checks | implemented, including the decode-from-wire path (`connect_packet_from`) |
| BR-INTER-011 | connection flood guard | implemented |
| BR-INTER-012 | packet and buffer size limits | implemented (framing, sim client receive buffer, encode limit) |
| BR-INTER-013 | declared-length validation (S4) | implemented in framing and in the decoder (typed `CodecError`, mutation-fuzzed under ASan/UBSan) |
| CAP-E02 / CAP-24 | generated codecs, multi-version, obfuscation | implemented: YAML reader, catalog tables, decode/encode over `FieldSpec`, obfuscator, sim client |
| SEC-S1 | no credential literals | implemented |

Dropped (architecture section 9): the hand-written inter-server length tables and handshakes.
Inter-server traffic is in-process now; BR-INTER-001..003, 007 belong to `gateway`/`persistence` seams.
Secure defaults chosen: an out-of-phase packet disconnects, an unsupported client version is refused
(no closest-version fallback), a 6th connection in a burst is rejected (legacy allowed 6).

## Build and test

    cmake -S . -B build -DNETPROTO_SANITIZE=ON && cmake --build build && ctest --test-dir build --output-on-failure
    ./build/netproto_tests --list     # rule-tag coverage

Everything builds with `-Wall -Wextra -Wpedantic -Werror` (`-DNETPROTO_WERROR=OFF` to relax) under g++ and clang++.
`ctest` runs ten tests:

| test | what |
|---|---|
| `acceptance` | the C++ binary `netproto_tests` (110 tests, `N passed, 0 failed, M expected-failures`) |
| `compat` | `netproto_compat_tests`: the gateway's and the sim's private codecs (their sources compiled read-only) against netproto, 12 tests |
| `yaml_differential` | the C++ YAML reader vs PyYAML on every `proto/*.yaml` and on small documents |
| `verify_legacy` | `netproto_codegen.py --verify-legacy`: re-evaluates the legacy headers, fails if `proto/legacy_20211103.yaml` or any entry of `client_packets.yaml` differs |
| `legacy_tools` | 28 Python tests: preprocessor subset, determinism, negative tests, the g++ cross-check (skipped only when `g++` is absent) |
| `verify_catalog` | `--verify-catalog`: `proto/packets_re_20211103.yaml` equals a fresh derivation, is internally consistent and contains the curated packets |
| `catalog_tools` | 28 Python tests: handler-layout rules, catalog counts / invariants / agreement with the legacy tables, negative tests |
| `layout_oracle` | regenerates `tests/golden/typed_re_20211103.txt` with g++ from the unmodified legacy structs and fails on any difference; also compares `sizeof` / `offsetof` |
| `legacy_ref_obfuscation`, `legacy_ref_positions` | the copied legacy routines still print the checked-in vectors |

The legacy tree is found above the unit directory, or via `RATHENA_LEGACY_ROOT`; with no legacy tree the
verify/oracle tests fail (they never pass vacuously). The gateway / tooling sources for `compat` are the sibling
units; without them that target is not built.

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
offsets, source line), 948 of 948 final opcodes, 534 struct sizes, 1973 field offsets, 480 header ids; 0
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
fields, 786 without (of which 269 have a handler and 517 have neither a handler nor a struct, only a length),
0 structs that could not be laid out (the extractor now handles `char names[N][M]`, anonymous member structs
and `uint16`/`uint32` bit groups; the 8 structs it used to reject are laid out and compiler-checked).
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

### What the legacy tables do not carry (still true)

- **Opcode obfuscation keys.** Deployment data, supplied through the environment (`obfuscation.keys_env`). The
  *schedule* is implemented (below). What is captured from `clif_shuffle.hpp` is only the block that is live for
  this target; the other per-build shuffle blocks are evaluated only when that target is generated.
- **Phase gates and message direction.** The legacy DB has neither; gates are the BR-MAP-059 rules, direction is
  derived from the packet name prefix and from "has a handler" (see "Decisions").
- **Per-field presence across versions.** A single target resolves every `#if`; the codec honours
  `FieldSpec::present_when` (tested on synthetic catalogs), but no generated data uses it until a second target is
  extracted.

## The full catalog (`proto/packets_re_20211103.yaml`)

`tools/legacy/derive_catalog.py` builds it from the same sources as the legacy evaluation plus
`src/common/packets.hpp`, `src/char/char_clif.cpp` (CH_ENTER) and the curated overlay `client_packets.yaml`;
`netproto_codegen.py --verify-catalog` (ctest `verify_catalog`) fails when the checked-in file is not byte-equal to
a fresh derivation. Every version entry carries `source:`.

| source | packets | meaning |
|---|---|---|
| `typed-struct` | 494 | a struct of `src/map/packets_struct.hpp` / `packets.hpp`: 346 server-to-client, 148 client-to-server: 475 `PACKET_*` structs by their `HEADER_` id and 19 `packet_*` structs by their `<name>Type` enumerator or the single `PacketType = ...` assignment in `clif.cpp` |
| `common-struct` | 50 | `src/common/packets.hpp`: login, char, zone hand-off (27 client-to-server, 23 server-to-client) |
| `handler-derived` | 109 | straight `RFIFO*` reads of the `clif_parse_*` body at literal / `pos[k]` offsets (rules below) |
| `handler` | 54 | has a handler, layout not mechanical: `fields: unknown`, offsets only |
| `length-only` | 479 | the packet DB lists only a length: `fields: unknown`, direction `any` |
| total | 1186 | |

Before this work the catalog had 9 packets (3 with fields, 6 `fields: unknown`). Of the legacy DB's 948 opcodes
162 have a typed struct, 269 have a handler without struct (164 handlers; 105 of those opcodes are older-layer
aliases of another opcode of the same handler and are listed as `also_accepted_by_legacy`, not accepted, the same
secure default as before), and 517 have neither (the README used to call the 786 untyped ones all
"handler-parsed"; only 269 are). A further 318 typed structs are not in the DB (server writers). Counted per packet
after folding: 544 typed (494 + 50), 109 handler-derived, 54 handler-unknown, 479 length-only. 15 extra struct names
share an opcode with another struct (`ZC_SAY_DIALOG`/`ZC_SAY_DIALOG2`, `ZC_UI_OPEN2`/`ZC_UI_OPEN_V3` ...): the
first declared one is the packet, the others are `also_named_by_legacy`. 34 `packet_*` structs have no opcode that
can be found mechanically (several are row structs such as `packet_quest_hunt_sub`; 3 disagree with the DB length:
`packet_dropflooritem` 24 vs 22, `packet_roulette_itemrecv_ack` 7 vs 5, `packet_roulette_open_ack` 25 vs 23); they
are reported by `derive_catalog.py`, not guessed. Three other top-level structs have no HEADER id or enumerator at all
(`ZC_STORE_ITEMLIST_NORMAL`, `ZC_STORE_ITEMLIST_EQUIP`, `ZC_PROGRESS_ACTOR`) and are not in the catalog either.

### Handler-derived layouts (`tools/legacy/handler_layout.py`)

A layout is derived only from a closed, straight-line reader: every packet read is `RFIFOB/W/L/Q/F(fd, OFF)`,
`RFIFOPOS/RFIFOPOS2(fd, OFF, ...)` or (once, in a variable packet, past every other field) `RFIFOP/RFIFOCP`; `OFF`
is an integer literal or `info->pos[k]` / `packet_db[...].pos[k]` resolved with the pos list of the opcode; no read is
inside a loop or under a preprocessor conditional; helpers of the same file that take `fd` are followed (depth 2)
when they are themselves mechanical; one width per offset, no overlaps, all reads inside the fixed length. Otherwise
`fields: unknown`. Types are the widths the macros read (unsigned; the macros carry no signedness), names are the
variable a read is assigned to or `f<offset>`, `RFIFOPOS` is `pos3` and `RFIFOPOS2` is `pos6`. Only bytes the legacy
handler reads are fields: an unread byte (e.g. offset 14 of CH_ENTER) is not decoded and is encoded as zero.
Each derived entry has a `# clif.cpp:LINE handler` comment. The 54 packets left unknown fail these rules: 18 read a
string/array of unknown width through a pointer, 14 read nothing from a longer fixed packet, 3 loop, the rest use
computed offsets, overlapping widths or helpers that are not mechanical.

### Provenance of the typed layouts

Typed layouts are flattened to `FieldSpec`s: nested structs and arrays of structs become dotted names that are the
C++ path (`slot.card`, `option_data[2].index`), bit fields become `bit_shift` / `bit_width` over their storage
integer, `char x[N][M]` becomes `N` strings, `u8[3]`/`u8[6]` members that `clif.cpp` fills with `WBUFPOS` /
`WBUFPOS2` (the names `PosDir`, `posDir`, `MoveData`, `moveData`) become `pos3` / `pos6`, a trailing flexible
member becomes a repeat group or a tail (a `char[]` tail is a NUL-terminated string, other tails are raw bytes), and a
variable packet that ends in a fixed-size array of structs (`list[MAX_ITEMLIST]` of the item lists) uses the array as
its repeat group. A struct that carries its own `packetLength`/`packetSize` word at offset 2 is variable-length even
when the DB has no entry for it (login/char packets).

### Compiler oracles

- `layout_oracle.py` writes, for each of the 544 typed structs, a g++ program against the unmodified legacy headers
  that fills the struct member by member with pseudo-random values (strings, arrays, nested structs, bit fields,
  flexible arrays; positions through the copied `WBUFPOS`/`WBUFPOS2`) and dumps its bytes:
  `tests/golden/typed_re_20211103.txt` (1700 field values + 876 row values). `test_golden.cpp` decodes every vector,
  compares each field with the value that was written and encodes it back to the identical bytes. The same
  programs print `sizeof` for 544 structs and `offsetof` for 1325 members, which must equal the catalog: 0
  disagreements. `crosscheck_gxx.py` keeps checking the packet DB (534 struct sizes, 1973 offsets, 480 headers, 0
  disagreements).
- Handler-derived layouts have no compiler oracle (the handlers cannot be run headless here); they are checked by
  unit tests of the rules, and 3 of them are used byte-for-byte by the sim's and gateway's own codecs (compat tests).

## Codec (`CodecSet::decode` / `encode`)

`decode(frame[, direction])` validates before it reads: at least 2 bytes, known opcode for that direction, fixed
length equal to the buffer, or (variable) length word present, `max(4, header, prefix+2) <= declared <= 32768` and
`declared == buffer size`; fields are bounds-checked again, a repeat group must tile the frame. Failures are a typed
`CodecError` (`code` = `CodecErrc`), or `try_decode` returns the code without throwing. `encode(packet)` finds the layout
by name and writes the frame; it refuses what it would have to truncate: a value outside the field's bit width (the
union of the signed and unsigned range is accepted, the decoder returns the field's own signedness), a string that
does not fit with its NUL, an embedded NUL, an unknown field name (a typo is an error, not a silent drop), a frame
over 32768 bytes. Missing values are zero / empty. A layout whose `fields` are unknown decodes to one `payload`
(the bytes after the opcode, or after the length word) and encodes it back.

Field kinds: u8/u16/u32/u64/i8/i16/i32/i64 (u64 as its bit pattern in an int64), `f32` (double), fixed NUL-padded
strings, raw bytes, integer arrays (1-byte arrays are `Bytes`, wider ones `vector<int64>`), `pos3`, `pos6`, bit fields,
tails, repeat groups (`DecodedPacket::repeats`), and `present_when` (a field exists only on clients matching the
predicate; the codec for a client is compiled from the layout with the other fields dropped). The gateway's
conventions (`[].name` row fields, `Raw` of width 0 = rest of the packet) are understood.

Properties tested: `decode(encode(x)) == x` for random values of every layout in the catalog (14,232 packets),
`encode(decode(f)) == f` for 544 legacy struct vectors, every truncation of a frame and every single bit flip decode to a
verdict without UB, and 100,000 mutated frames (bit flips, truncation, extension, wrecked length words, other
opcodes, glued frames, random bodies) pass the invariants of `fuzz/decode_invariants.hpp` under ASan+UBSan.
The libFuzzer target `fuzz/fuzz_decode.cpp` (clang `-fsanitize=fuzzer`) shares that body; it is built with
`-DNETPROTO_FUZZ=ON`. Here clang 18 has no libFuzzer / ASan runtime installed, so the target is only
syntax-checked and the deterministic mutation test is what runs.

## YAML: runtime loader or generated tables (decision)

Both, from the same files. `load_catalog_yaml` (`yaml.hpp` + `spec_yaml.cpp`, ~400 lines, a documented YAML subset
that rejects what it does not support with a line number and refuses unknown keys) is for tools, tests and the sim
client, which can read the file at start-up. `netproto_codegen.py --emit-catalog` turns the same file into compact C++
row tables (`catalog_tables.hpp`) for servers: no file access, no parse at start-up, no dependence on the proto
directory at run time, and the build still needs only Python 3 + PyYAML. A test proves the loaded catalog and the
compiled tables are identical for both proto files; the reader is also compared with PyYAML on every proto file.
Why not only one: a server should not parse YAML on start, and a codegen-only design leaves the sim and tools unable
to load a spec that was not compiled in.

## Obfuscation (`OpcodeObfuscator`)

The schedule of `src/map/clif.cpp` (`clif_parse`, `clif_parse_WantToConnection`), with `f(x) = x*k1 + k2 (mod 2^32)`:
the first packet (no session yet) is de-obfuscated with `((f(k0) >> 16) & 0x7FFF)`; the connect packet then sets the
session key `f(f(k0))`; every later packet uses `(key >> 16) & 0x7FFF` and advances `key = f(key)` only after a valid
frame, so `peek_decode` does not advance and `commit` does. One object per session and direction; the client
(`SimClient`) walks the same schedule with `encode`. Expected vectors are printed by
`tests/legacy_ref/obfuscation_ref.cpp`, a copy of those lines (licence note in the file) compiled into a tiny program
(4 key sets x 40 opcodes, FAKE keys; one set is the degenerate `{1,2,3}` whose masks stay 0 for 16 packets). Keys come from
`keys_from_env(NAME)` (`NAME` = `obfuscation.keys_env`, format `0xAAAAAAAA,0xBBBBBBBB,0xCCCCCCCC`); they are never in
config or proto data (SEC-S1 tests), and an unset variable means "no keys", never a default. The old header
described the keys as "seed, multiplier, addend"; the legacy meaning is `clif_cryptKey[0..2]` as above.

## Sim client (`SimClient`)

An in-process client over the codec: `send(packet)` encodes by name (only client-to-server packets), checks the phase
gate when `enforce_gates` is set (PreSession -> `ZC_ACCEPT_ENTER*` received -> PreMapLoad -> `CZ_NOTIFY_ACTORINIT` sent
-> InWorld; `ZC_REFUSE_ENTER` / `SC_NOTIFY_BAN` close it) and obfuscates the opcode when keyed; `receive(bytes)` takes
any slice of the server stream, buffers partial frames, splits with the server frame table (typed packets and
length-only `OPAQUE_xxxx` frames, which decode to a `payload`), and closes the client on an unknown opcode, an invalid
length, an undecodable frame or a 64 KiB overrun instead of throwing. The two-argument constructor keeps the scaffold's
behaviour (gates tracked, not enforced) so the scaffold's tests are unchanged.

## Compatibility with the gateway and the sim (`tests/compat`)

The sources `gateway/src/wire.cpp` and `tooling/src/sim_wire.cpp` are compiled read-only into `netproto_compat_tests`.
Result: for every packet of the gateway's table (59, 55 with a comparable layout) both interpreters produce the
same frame for 200 random packets and the same decode (the gateway catalog is run through `CodecSet` with its two overlay
layouts promoted); both accept and reject the same truncated frames and wrong declared lengths; all 29 packets of the
sim's table encode to byte-identical frames (300 random argument sets each), the sim's decoder reads back what
netproto decodes, and the sim's and netproto's framing split the same mixed streams. Both consuming units still
build and pass their own tests against this netproto (gateway 220/220; tooling 23 pass / 16 expected-failure / 59 skipped
here because no Luau is installed). `netproto` keeps its source files' link surface: the codec is inside
`codec_set.cpp`, the position and obfuscation arithmetic is inline in headers and `build_catalog` is inline, because
gateway and tooling compile a fixed list of netproto `.cpp` files.

Findings (which side is right, per the legacy headers):

1. `ZC_REFUSE_ENTER`: the gateway uses opcode 0x006a with 3 bytes. In the legacy packet DB 0x006a is 23 bytes
   (`AC_REFUSE_LOGIN`; `clif_parse_WantToConnection` writes `packet_len(0x6a)` bytes), `ZC_REFUSE_ENTER` is 0x0074 (3 bytes,
   `PACKET_ZC_REFUSE_ENTER`, what the sim uses) and `clif_authfail_fd` sends `SC_NOTIFY_BAN` 0x0081. netproto's catalog
   follows the legacy structs; the gateway's 0x006a/3-byte frame is wrong on a real client.
2. `CHARACTER_INFO.level` and `.bIsChangedCharName` are `int16` in the legacy struct; the gateway reads them unsigned.
   netproto is right (values are small, no observable difference today).
3. `CA_SSO_LOGIN_REQ.token` and `AC_ACK_HASH.salt` are `char[]`; the gateway keeps raw bytes, netproto a NUL-terminated string tail.
4. `PACKET_TC_RESULT` has a `packetLength` word: variable in the legacy header (netproto), fixed(34) in the gateway; both accept
   the 34-byte frame.
5. `ZC_NOTIFY_PLAYERMOVE.moveData` is a position (6 packed bytes, `WBUFPOS2`); the gateway ignores `FieldSpec::array_len` in
   its interpreter (decodes one byte, refuses a byte array). It never sends that packet.
6. Names: `auth_code` is `login_id1` in `clif_parse_WantToConnection`; `SC_NOTIFY_BAN.errorCode`, `ZC_REFUSE_ENTER.errorCode`, `new_`
   (`new` is a C++ keyword); the gateway's `CH_ENTER.unknown` (u16 at 14) is not read by the char server.
7. Behaviour: the gateway's `encode` silently cuts a string to width-1, netproto refuses it (`kBadValue`). `gateway/src/wire.cpp:401`
   calls `memcpy(dst, nullptr, 0)` for an empty raw tail (UBSan reports it; the compat tests never encode an empty tail).
8. Sim: it names 0x09ff / 0x09fd `ZC_NOTIFY_STANDENTRY11` / `ZC_NOTIFY_MOVEENTRY11` and 0x0438 `CZ_USE_SKILL`; no legacy source names
   them, the catalog says `LEGACY_IDLE_UNIT`, `LEGACY_UNIT_WALKING`, `CZ_USE_SKILL2`. 0x008e (`ZC_NOTIFY_PLAYERCHAT`) has no struct and no handler:
   `OPAQUE_008E` (payload only). Opcodes, lengths, gates and every byte agree.
9. Legacy bug seen on the way: `RFIFOPOS2` in `clif.cpp` reads through `WFIFOP` (the write buffer); nothing calls it.

## Decisions where the spec was silent

- Aliased opcodes of one handler are not accepted (only the opcode of the curated overlay, else the last one defined);
  length-only frames are direction `any` and never valid client packets.
- Names: curated overlay > struct `HEADER_` name > `/// XXXX ... (NAME)` doc comment of `clif.cpp` > `LEGACY_<STRUCT>` /
  `LEGACY_<HANDLER>` / `OPAQUE_<HEX>`. Nothing is invented as a protocol name.
- Direction: `CZ_/CA_/CH_/CT_`, `PING` are client-to-server, `ZC_/HC_/AC_/SC_/TC_` server-to-client; a handler means client-to-server.
- Gates: login-phase packets PreSession, other char packets PreMapLoad (the gateway's table), `CZ_ENTER` / `ZC_ACCEPT_ENTER*` /
  `ZC_REFUSE_ENTER` PreSession, `CZ_NOTIFY_ACTORINIT` PreMapLoad, the rest InWorld. These are policy, not legacy data.
- A variable packet's `header` is at least the end of its last fixed field: a frame that declares less than the fields it
  must contain is refused (legacy would read past the declared length).
- Two structs disagree with their DB length (`PACKET_ZC_FEED_PET` / `_MER`, 7 vs 5): the struct size is used (what the legacy
  writer sends), the DB value is kept as `legacy_db_length`.
- `encode` is strict (see Codec); `decode` of a string tail without a NUL is accepted (text policing is the handler's job).

## Not done

- Handler-derived layouts have no independent oracle beyond rule tests; 54 handlers (and 479 length-only frames) stay unknown.
- 34 `packet_*` structs and 3 other-named structs of `packets_struct.hpp` have no mechanically findable opcode; they are not in the catalog.
- One client target only (`re/20211103`); `main/20211103` (hypothetical) has the 9 curated packets but no full catalog, and `present_when`
  is exercised by synthetic tests only.
- libFuzzer was not run (no runtime library on this machine); the deterministic mutation fuzz and the sanitizers are.
- `CodecSet` copies layouts per client version; there is no cross-version "closest layout" fallback (by design).
