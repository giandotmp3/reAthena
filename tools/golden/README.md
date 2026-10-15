# Golden vectors

The reimagined rules in `modernized/rathena-reimagined/` are pinned to what the legacy code
actually computes. This directory captures those values from the real legacy servers.

```
tools/golden/capture.sh            # everything: rng, re, prere
tools/golden/capture.sh re         # one mode
```

| Mode | What runs | Output |
|---|---|---|
| `rng` | `rng_capture.cpp` built against `src/common/random.cpp` | `core/tests/golden/rng.jsonl`: the seeded legacy stream |
| `re` | renewal login/char/map servers | `map-rules/tests/golden/aspd_re_re.jsonl`: `status_base_amotion_pc` |
| `prere` | pre-renewal servers (`PRERE`) | `map-rules/tests/golden/defense_{pc,mob}_pre_re.jsonl`: `battle_calc_defense_reduction` |

How a server capture works:

1. The servers are built into `build-golden/<mode>/` with `-DRATHENA_GOLDEN_TRACE`. The binaries in
   the repository root are not touched. Without that define the hooks (`GOLDEN_TRACE(...)` from
   `src/common/golden_trace.hpp`) compile to nothing, so normal builds carry no trace code.
2. They run on a private SQLite database under `build-golden/<mode>/save`, with their own config
   files that import the defaults (`conf/import` is left alone) and `RATHENA_RNG_SEED` fixed.
3. `tools/ci/smoke_client.py` logs a character in and stays online. `golden_driver.txt` takes over
   on login: in renewal it walks classes × weapons (with and without a shield) × AGI × DEX; in
   pre-renewal it has monsters of varied VIT/DEF fight the character over a range of VIT.
4. Each traced call appends one JSON line to `RATHENA_GOLDEN_TRACE_FILE`. The script writes the
   de-duplicated, sorted vectors per kind into the test trees.

Adding a vector kind: put a `GOLDEN_TRACE( "kind", { "field", value }, ... )` where the legacy code
computes the value (record the inputs the modern rule takes and the result), drive it from
`golden_driver.txt`, and read the new `<kind>_<mode>.jsonl` from a test (see
`map-rules/tests/golden_vectors.hpp`).

Capturing needs cmake, a C++17 compiler and python3, and ports 6900/6121/5121 free; a server
capture takes several minutes (mostly the build). Commit the regenerated `.jsonl` files together
with the rule change they pin.
