#!/usr/bin/env bash
# Build the legacy map-server with the record/replay hook, entirely outside the repository.
#   [RA_PRERENEWAL=1] build_legacy.sh <build-dir> [repo-root]   (RA_PRERENEWAL=1 builds the pre-renewal server mode)
# Copies the legacy source to <build-dir>/rA (the repo tree is never touched), applies
# patch/legacy-replay-hook.patch to the COPY, configures with CMake+Ninja (SQLite backend, no MySQL/PostgreSQL needed)
# and builds map-server into <build-dir>/rA/map-server. Needs: cmake >= 3.13, ninja or make, g++ with C++20, zlib, patch, tar.
set -euo pipefail
B="$(mkdir -p "${1:?usage: build_legacy.sh <build-dir> [repo]}" && cd "$1" && pwd)"
REPO="${2:-/home/user/reAthena}"
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ ! -d "$B/rA/src" ]; then
	mkdir -p "$B/rA"
	(cd "$REPO" && tar --exclude=.git --exclude=./modernized --exclude=./analysis -cf - .) | tar -xf - -C "$B/rA"
	mkdir -p "$B/rA/modernized"
	cp -r "$REPO/modernized/rathena-reimagined" "$B/rA/modernized/"   # map-rules library the legacy CMake build links
fi
if [ "${RA_PRERENEWAL:-0}" = 1 ]; then   # pre-renewal server mode: comment out "#define RENEWAL" in the COPY (loads npc/pre-re/scripts_main.conf)
	sed -i 's|^#define RENEWAL$|//#define RENEWAL|' "$B/rA/src/config/renewal.hpp"
fi
if [ ! -e "$B/rA/src/map/replay_hook.inc" ]; then
	patch -p1 -d "$B/rA" < "$HERE/patch/legacy-replay-hook.patch"
fi
for f in "$B"/rA/conf/import-tmpl/*; do [ -e "$B/rA/conf/import/$(basename "$f")" ] || { mkdir -p "$B/rA/conf/import"; cp "$f" "$B/rA/conf/import/"; }; done
for f in "$B"/rA/db/import-tmpl/*; do [ -e "$B/rA/db/import/$(basename "$f")" ] || { mkdir -p "$B/rA/db/import"; cp -r "$f" "$B/rA/db/import/"; }; done
GEN=Ninja; command -v ninja >/dev/null || GEN="Unix Makefiles"
cmake -S "$B/rA" -B "$B/cm" -G "$GEN" -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$B/rA" -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="$B/lib" -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY="$B/lib" > "$B/cmake.log" 2>&1
cmake --build "$B/cm" --target map-server --parallel "$(nproc)" > "$B/build.log" 2>&1 || { tail -30 "$B/build.log"; exit 1; }
echo "built $B/rA/map-server"
