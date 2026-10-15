# Script buildin corpus

Scripts that run the legacy script VM headless and assert what its buildins return. They pin the
VM's behaviour while it is wrapped behind `ScriptHost` (`src/map/script_host.hpp`,
REIMAGINED_ARCHITECTURE.md §3.2, map-server step 2).

```
tools/ci/script_corpus.sh                         # needs ./map-server (CMake or autotools build)
tools/ci/script_corpus.sh build-golden/re/bin     # or a directory holding map-server
python3 tools/ci/script_corpus_coverage.py --missing
```

`script_corpus.sh` writes a private configuration and SQLite database under `corpus-run/`
(`conf/import` and `save/` are left alone), adds one `npc:` line per `*.txt` file here and starts
`map-server --run-once`. Each test NPC runs its checks in `OnInit`, with no player attached, and
prints `CORPUS PASS <test>` or `CORPUS FAIL <test> <check>: got '…', want '…'` through `debugmes`.
The runner fails on any FAIL line, on a test without a PASS line (a script error ends the script
before it) and on a non-zero exit; it ends with the coverage against the buildin inventory
`doc/script_buildins.tsv` (`tools/codegen/script_buildins.py`). The full `npc/` set loads too.

Adding a test: a `-	script	corpus_<name>	-1,{ OnInit: … }` NPC that calls
`callfunc "corpus_begin", "<name>";`, then `corpus_eq` / `corpus_ok` (see `_lib.txt`), then
`callfunc "corpus_end";`. Assert what legacy does, quirks included, and say so in a comment
(e.g. `ceil(1200, 100)` is 1300). Commands that need a player or a client are out of reach here.

The map server binds port 5121 while it starts; nothing else may listen on it.
