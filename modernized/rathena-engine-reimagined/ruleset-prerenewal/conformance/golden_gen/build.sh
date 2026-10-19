#!/usr/bin/env bash
# Builds the pre-renewal legacy map-server with the golden hook, entirely outside the repository.
#   build.sh <build-dir> [repo-root]      -> <build-dir>/rA/map-server
# Same approach as tooling/m0/replay/build_legacy.sh (RA_PRERENEWAL=1): copy the tree (never touching the repo), comment out
# `#define RENEWAL` in the COPY, then apply apply_edits.py (random-draw log, stage probes, hook include) and drop in
# golden_hook.inc / golden_stage.hpp (basic attack) and golden_status_hook.inc (status calculation; GOLDEN_PRERE=1 builds strict PRERE). Needs cmake >= 3.13, ninja or make, g++ with C++20, zlib, python3.
set -euo pipefail
B="$(mkdir -p "${1:?usage: build.sh <build-dir> [repo]}" && cd "$1" && pwd)"
REPO="${2:-/home/user/reAthena}"
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ ! -d "$B/rA/src" ]; then
	mkdir -p "$B/rA"
	(cd "$REPO" && tar --exclude=.git --exclude=./modernized --exclude=./analysis -cf - .) | tar -xf - -C "$B/rA"
	mkdir -p "$B/rA/modernized"
	cp -r "$REPO/modernized/rathena-reimagined" "$B/rA/modernized/"   # map-rules library the legacy CMake links
fi
grep -q '^#define RENEWAL$' "$B/rA/src/config/renewal.hpp" && sed -i 's|^#define RENEWAL$|//#define RENEWAL|' "$B/rA/src/config/renewal.hpp"   # pre-renewal server mode (sed only when needed: it would touch the mtime and rebuild everything)
if [ "${GOLDEN_PRERE:-0}" = 1 ]; then   # strict pre-renewal (what `configure --enable-prere` builds): no RENEWAL_* flag at all (status golden)
	grep -q '^//#define PRERE$' "$B/rA/src/config/renewal.hpp" && sed -i 's|^//#define PRERE$|#define PRERE|' "$B/rA/src/config/renewal.hpp"
fi
for f in golden_stage.hpp golden_hook.inc golden_status_hook.inc; do cmp -s "$HERE/$f" "$B/rA/src/map/$f" || cp "$HERE/$f" "$B/rA/src/map/$f"; done   # (keep mtimes of unchanged files)
python3 -I "$HERE/st_extract.py" incs --status-hpp "$B/rA/src/map/status.hpp" --out "$B/rA/src/map.new" > /dev/null   # status table + constants for golden_status_hook.inc
for f in "$B"/rA/src/map.new/*.inc; do cmp -s "$f" "$B/rA/src/map/$(basename "$f")" || cp "$f" "$B/rA/src/map/$(basename "$f")"; done; rm -rf "$B/rA/src/map.new"
python3 -I "$HERE/apply_edits.py" "$B/rA"
for f in "$B"/rA/conf/import-tmpl/*; do [ -e "$B/rA/conf/import/$(basename "$f")" ] || { mkdir -p "$B/rA/conf/import"; cp "$f" "$B/rA/conf/import/"; }; done
for f in "$B"/rA/db/import-tmpl/*; do [ -e "$B/rA/db/import/$(basename "$f")" ] || { mkdir -p "$B/rA/db/import"; cp -r "$f" "$B/rA/db/import/"; }; done
GEN=Ninja; command -v ninja >/dev/null || GEN="Unix Makefiles"
cmake -S "$B/rA" -B "$B/cm" -G "$GEN" -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$B/rA" -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="$B/lib" -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY="$B/lib" > "$B/cmake.log" 2>&1
cmake --build "$B/cm" --target map-server --parallel "$(nproc)" > "$B/build.log" 2>&1 || { tail -30 "$B/build.log"; exit 1; }
echo "built $B/rA/map-server"
