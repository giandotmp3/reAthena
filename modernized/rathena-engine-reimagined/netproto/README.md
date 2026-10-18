# netproto

Wire-protocol layer of rathena-engine (see `analysis/rathena-engine/REIMAGINED_ARCHITECTURE.md`
sections 2, 4, 8 and the spec section 4.1): packet spec, codegen, per-session codec sets,
framing, obfuscation, phase gating, anti-cheat framing rules, sim client, fuzz targets.
One client `(date, flavour)` is generated first (`main/20211103`); the codec table is keyed
by version so more can be added as data.

Stack: C++20, CMake, no third-party dependencies. Codegen is Python 3 + PyYAML.

## Layout

- `proto/client_packets.yaml` machine-readable spec (starter set; opcodes verified against the legacy packet DB before more versions are generated)
- `tools/codegen/netproto_codegen.py` validate + resolve + emit frame tables (field codecs pending)
- `include/netproto/` domain model (`spec.hpp`), framing, gating, connect check, flood guard, codec set, obfuscation, sim client
- `fuzz/` libFuzzer targets (`-DNETPROTO_FUZZ=ON`, clang)
- `tests/` acceptance tests, tagged by rule ID

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

`PENDING_TEST` is expected-failure: it must fail now; if it starts passing the run fails (XPASS)
so the marker is removed. Obfuscation keys are deployment data supplied through the environment
(`NETPROTO_KEYS_*`); tests use fake keys only.
