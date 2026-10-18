#!/usr/bin/env bash
# Re-run every corpus on both sides and print the strict-pass lines.
#   run_all.sh <renewal-build> <pre-renewal-build> <luau-cli> [--skip-legacy]
# (builds: build_legacy.sh; the legacy side of the 5 corpora takes ~45 min on 4 cores, the Luau side ~20 s)
set -euo pipefail
B="$1"; P="$2"; L="$3"; shift 3
H="$(cd "$(dirname "$0")" && pwd)"
run() { python3 -I "$H/run_replay.py" --luau "$L" "$@" 2>&1 | grep -E "^CASES|excluded|strict pass \(|NPCs with all"; }
echo "# renewal corpus.json";        run --build "$B" "$@"
echo "# renewal wide";               run --build "$B" --corpus "$H/corpus/corpus_wide.json" --set re --report-name wide "$@"
echo "# renewal hunt";               run --build "$B" --corpus "$H/corpus/corpus_hunt.json" --set re --report-name hunt "$@"
echo "# renewal synthetic";          run --build "$B" --synthetic --set re --report-name synth "$@"
echo "# pre-renewal wide";           run --build "$P" --corpus "$H/corpus/corpus_wide.json" --set pre --report-name wide "$@"
echo "# pre-renewal hunt";           run --build "$P" --corpus "$H/corpus/corpus_hunt.json" --set pre --report-name hunt "$@"
echo "# pre-renewal synthetic";      run --build "$P" --synthetic --set pre --report-name synth "$@"
