#!/usr/bin/env bash
# Builds the RENEWAL legacy map-server with the golden hook, entirely outside the repository.
#   build.sh <build-dir> [repo-root]      -> <build-dir>/rA/map-server
# Same approach as ruleset-prerenewal/conformance/golden_gen/build.sh, minus the step that disables RENEWAL: the copy of the
# tree keeps `#define RENEWAL` (and every RENEWAL_* sub-flag) on, so the binary loads db/re and compiles the #ifdef RENEWAL branches.
# apply_edits.py (random-draw log, stage probes, hook include) is applied to the COPY; golden_hook.inc / golden_stage.hpp are dropped in.
# Needs cmake >= 3.13, ninja or make, g++ with C++20, zlib, python3.
set -euo pipefail
B="$(mkdir -p "${1:?usage: build.sh <build-dir> [repo-root]}" && cd "$1" && pwd)"
REPO="${2:-/home/user/reAthena}"
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ ! -d "$B/rA/src" ]; then
	mkdir -p "$B/rA"
	(cd "$REPO" && tar --exclude=.git --exclude=./modernized --exclude=./analysis -cf - .) | tar -xf - -C "$B/rA"
	mkdir -p "$B/rA/modernized"
	cp -r "$REPO/modernized/rathena-reimagined" "$B/rA/modernized/"   # map-rules library the legacy CMake links
fi
grep -q '^#define RENEWAL$' "$B/rA/src/config/renewal.hpp" || { echo "renewal.hpp: RENEWAL is not defined in the copy"; exit 1; }
cp "$HERE/golden_stage.hpp" "$HERE/golden_hook.inc" "$B/rA/src/map/"
python3 -I "$HERE/apply_edits.py" "$B/rA"
for f in "$B"/rA/conf/import-tmpl/*; do [ -e "$B/rA/conf/import/$(basename "$f")" ] || { mkdir -p "$B/rA/conf/import"; cp "$f" "$B/rA/conf/import/"; }; done
for f in "$B"/rA/db/import-tmpl/*; do [ -e "$B/rA/db/import/$(basename "$f")" ] || { mkdir -p "$B/rA/db/import"; cp -r "$f" "$B/rA/db/import/"; }; done
GEN=Ninja; command -v ninja >/dev/null || GEN="Unix Makefiles"
cmake -S "$B/rA" -B "$B/cm" -G "$GEN" -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$B/rA" -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="$B/lib" -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY="$B/lib" > "$B/cmake.log" 2>&1
cmake --build "$B/cm" --target map-server --parallel "$(nproc)" > "$B/build.log" 2>&1 || { tail -30 "$B/build.log"; exit 1; }
echo "built $B/rA/map-server"
