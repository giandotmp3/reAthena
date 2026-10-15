#!/usr/bin/env bash
# Captures golden vectors from the legacy servers for the reimagined rules' tests.
#
#   tools/golden/capture.sh [re|prere|rng|all]      (default: all)
#
# rng: builds tools/golden/rng_capture.cpp against src/common/random.cpp and records the seeded
# legacy random stream into modernized/rathena-reimagined/core/tests/golden/rng.jsonl.
#
# For each mode it builds login/char/map servers with RATHENA_GOLDEN_TRACE (and PRERE for
# pre-renewal) into build-golden/<mode>/ -- the regular binaries in the repository root are not
# touched -- starts them on a private SQLite database with a fixed RATHENA_RNG_SEED, logs one
# character in with tools/ci/smoke_client.py and lets tools/golden/golden_driver.txt exercise
# the traced functions. The traced calls are written, de-duplicated and sorted, to
# modernized/rathena-reimagined/map-rules/tests/golden/<kind>.jsonl.
#
# Needs: cmake, a C++17 compiler, python3; nothing may be listening on ports 6900/6121/5121.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
modes="${1:-all}"
[ "$modes" = "all" ] && modes="rng re prere"
out_dir="$root/modernized/rathena-reimagined/map-rules/tests/golden"
mkdir -p "$out_dir"

capture() {
	local mode="$1" dir="$root/build-golden/$1" flags="-DRATHENA_GOLDEN_TRACE"
	[ "$mode" = "prere" ] && flags="$flags -DPRERE"

	echo "== $mode: building"
	mkdir -p "$dir"
	cmake -S "$root" -B "$dir" -DCMAKE_BUILD_TYPE=Release -DWITH_POSTGRESQL=OFF \
		-DCMAKE_CXX_FLAGS="$flags" \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$dir/bin" \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY="$dir/lib" \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY="$dir/lib" > "$dir.configure.log"
	cmake --build "$dir" --parallel "$(nproc 2>/dev/null || echo 4)" \
		--target login-server char-server map-server > "$dir.build.log"

	# Private configuration: the defaults plus capture overrides; conf/import is left alone.
	local conf="$dir/conf" save="$dir/save" logs="$dir/logs"
	rm -rf "$save" "$logs"
	mkdir -p "$conf" "$save" "$logs"
	printf 'import: conf/inter_athena.conf\ndb_backend: sqlite\nsqlite_directory: %s\n' "$save" > "$conf/inter.conf"
	printf 'import: conf/login_athena.conf\nnew_account: yes\n' > "$conf/login.conf"
	printf 'import: conf/char_athena.conf\npincode_enabled: no\n' > "$conf/char.conf"
	printf 'import: conf/map_athena.conf\nnpc: tools/golden/golden_driver.txt\n' > "$conf/map.conf"

	local trace="$dir/trace.jsonl"
	: > "$trace"
	export RATHENA_RNG_SEED=20261015 RATHENA_GOLDEN_TRACE_FILE="$trace"

	# The servers chdir to the directory of argv[0]; name them as if they lived in the root.
	local pids=()
	(exec -a "$root/golden-login-server" "$dir/bin/login-server" --login-config "$conf/login.conf" --inter-config "$conf/inter.conf") > "$logs/login.log" 2>&1 &
	pids+=($!)
	(exec -a "$root/golden-char-server" "$dir/bin/char-server" --char-config "$conf/char.conf" --inter-config "$conf/inter.conf") > "$logs/char.log" 2>&1 &
	pids+=($!)
	(exec -a "$root/golden-map-server" "$dir/bin/map-server" --map-config "$conf/map.conf" --inter-config "$conf/inter.conf") > "$logs/map.log" 2>&1 &
	pids+=($!)
	trap 'kill "${pids[@]}" 2>/dev/null || true' EXIT

	for _ in $(seq 1 180); do
		grep -q "and listening on port" "$logs/map.log" 2>/dev/null && break
		sleep 2
	done
	sleep 3

	echo "== $mode: driving"
	python3 tools/ci/smoke_client.py --register --user golden1 --password goldenpw1 --char Golden \
		--stay 1800 --until-file "$logs/map.log" --until-text "GOLDEN DONE"
	kill "${pids[@]}" 2>/dev/null || true
	wait 2>/dev/null || true
	trap - EXIT

	if ! grep -q "GOLDEN DONE" "$logs/map.log"; then
		echo "capture.sh: the driver did not finish (see $logs/map.log)" >&2
		exit 1
	fi

	# One file per kind, de-duplicated and sorted so re-captures diff cleanly.
	python3 - "$trace" "$out_dir" "$mode" <<'EOF'
import json, sys, collections
trace, out_dir, mode = sys.argv[1:]
by_kind = collections.defaultdict(set)
for line in open(trace):
    rec = json.loads(line)
    kind = rec.pop("kind")
    by_kind[kind].add(json.dumps(rec, sort_keys=True))
for kind, rows in by_kind.items():
    name = f"{kind}_{'pre_re' if mode == 'prere' else 're'}.jsonl"
    with open(f"{out_dir}/{name}", "w") as f:
        f.write("".join(r + "\n" for r in sorted(rows)))
    print(f"   {name}: {len(rows)} vectors")
EOF
}

capture_rng() {
	local dir="$root/build-golden/rng"
	mkdir -p "$dir" "$root/modernized/rathena-reimagined/core/tests/golden"
	"${CXX:-c++}" -std=c++17 -O1 -I"$root/src" -I"$root/src/common" \
		"$root/tools/golden/rng_capture.cpp" "$root/src/common/random.cpp" -o "$dir/rng_capture"
	"$dir/rng_capture" > "$root/modernized/rathena-reimagined/core/tests/golden/rng.jsonl"
	echo "   rng.jsonl: $(wc -l < "$root/modernized/rathena-reimagined/core/tests/golden/rng.jsonl") draws"
}

for m in $modes; do
	if [ "$m" = "rng" ]; then capture_rng; else capture "$m"; fi
done
