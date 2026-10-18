#!/usr/bin/env bash
# Run the dump-hook map-server headless and write the post-load DB dumps.
#   run_dump.sh <build-dir> <dump-out-dir> [overlay-dir]
# <build-dir> is what build_dump.sh built. If an overlay directory is given, its *.yml files are copied over
# <build-dir>/rA/db/import/ (the COPY of the tree, never the repo) before the run; without it the pristine
# import-tmpl files (empty overlays) are restored, so a baseline run is always reproducible.
set -euo pipefail
B="$(cd "${1:?usage: run_dump.sh <build-dir> <dump-out-dir> [overlay-dir]}" && pwd)"
OUT="$(mkdir -p "${2:?}" && cd "$2" && pwd)"
OVERLAY="${3:-}"
RA="$B/rA"
for f in "$RA"/db/import-tmpl/*; do cp -r "$f" "$RA/db/import/"; done
if [ -n "$OVERLAY" ]; then cp -r "$OVERLAY"/. "$RA/db/import/"; fi
RUN="$OUT/run"; rm -rf "$RUN"; mkdir -p "$RUN/save"
printf 'import: conf/inter_athena.conf\ndb_backend: sqlite\nsqlite_directory: %s\n' "$RUN/save" > "$RUN/inter.conf"
printf 'import: conf/map_athena.conf\nmap_port: 5391\n' > "$RUN/map.conf"
cd "$RA"
RA_DBDUMP_DIR="$OUT" ./map-server --run-once --map-config "$RUN/map.conf" --inter-config "$RUN/inter.conf" > "$OUT/server.log" 2>&1 || true
rm -rf "$RUN"
ls "$OUT"/*.jsonl >/dev/null 2>&1 || { echo "no dump produced, see $OUT/server.log" >&2; exit 1; }
echo "dump written to $OUT"
