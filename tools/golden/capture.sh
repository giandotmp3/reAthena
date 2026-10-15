#!/usr/bin/env bash
# Captures golden vectors from the legacy servers for the reimagined rules' tests.
#
#   tools/golden/capture.sh [rng|re|prere|chardel|all]      (default: all)
#
# rng:     builds tools/golden/rng_capture.cpp against src/common/random.cpp and records the
#          seeded legacy random stream into modernized/rathena-reimagined/core/tests/golden/rng.jsonl.
# re, prere: renewal / pre-renewal servers driven by tools/golden/golden_driver.txt; vectors go to
#          modernized/rathena-reimagined/map-rules/tests/golden/<kind>_<mode>.jsonl.
# chardel: renewal servers; tools/golden/golden_chardel.txt fills the tables a character delete
#          cleans up, then the character is deleted through the client packet and the SQL the
#          char-server ran goes, in order, to
#          modernized/rathena-reimagined/persistence/tests/golden/char_delete_sql.jsonl.
#
# Server modes build login/char/map with RATHENA_GOLDEN_TRACE (and PRERE for pre-renewal) into
# build-golden/<build>/ -- the regular binaries in the repository root are not touched -- and run
# them on a private SQLite database with a fixed RATHENA_RNG_SEED.
#
# Needs: cmake, a C++17 compiler, python3; nothing may be listening on ports 6900/6121/5121.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
modes="${1:-all}"
[ "$modes" = "all" ] && modes="rng re prere chardel"
reimagined="$root/modernized/rathena-reimagined"

# build_servers <re|prere>: trace-enabled login/char/map into build-golden/<build>/bin
build_servers() {
	local build="$1" dir="$root/build-golden/$1" flags="-DRATHENA_GOLDEN_TRACE"
	[ "$build" = "prere" ] && flags="$flags -DPRERE"
	echo "== $build: building"
	mkdir -p "$dir"
	cmake -S "$root" -B "$dir" -DCMAKE_BUILD_TYPE=Release -DWITH_POSTGRESQL=OFF \
		-DCMAKE_CXX_FLAGS="$flags" \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$dir/bin" \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY="$dir/lib" \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY="$dir/lib" > "$dir.configure.log"
	cmake --build "$dir" --parallel "$(nproc 2>/dev/null || echo 4)" \
		--target login-server char-server map-server > "$dir.build.log"
}

# start_servers <build> <run> <driver npc> [extra char conf lines]: starts the servers on a
# private configuration (the defaults plus capture overrides; conf/import is left alone) in
# build-golden/<run>/. Sets $run_dir, $logs, $trace and $pids.
start_servers() {
	local build="$1" run="$2" driver="$3" char_extra="${4:-}"
	local bin="$root/build-golden/$build/bin"
	run_dir="$root/build-golden/$run"
	local conf="$run_dir/conf" save="$run_dir/save"
	logs="$run_dir/logs"
	trace="$run_dir/trace.jsonl"
	rm -rf "$save" "$logs"
	mkdir -p "$conf" "$save" "$logs"
	printf 'import: conf/inter_athena.conf\ndb_backend: sqlite\nsqlite_directory: %s\n' "$save" > "$conf/inter.conf"
	printf 'import: conf/login_athena.conf\nnew_account: yes\n' > "$conf/login.conf"
	printf 'import: conf/char_athena.conf\npincode_enabled: no\n%s' "$char_extra" > "$conf/char.conf"
	printf 'import: conf/map_athena.conf\nnpc: %s\n' "$driver" > "$conf/map.conf"
	: > "$trace"
	export RATHENA_RNG_SEED=20261015 RATHENA_GOLDEN_TRACE_FILE="$trace"

	# The servers chdir to the directory of argv[0]; name them as if they lived in the root.
	pids=()
	(exec -a "$root/golden-login-server" "$bin/login-server" --login-config "$conf/login.conf" --inter-config "$conf/inter.conf") > "$logs/login.log" 2>&1 &
	pids+=($!)
	(exec -a "$root/golden-char-server" "$bin/char-server" --char-config "$conf/char.conf" --inter-config "$conf/inter.conf") > "$logs/char.log" 2>&1 &
	pids+=($!)
	(exec -a "$root/golden-map-server" "$bin/map-server" --map-config "$conf/map.conf" --inter-config "$conf/inter.conf") > "$logs/map.log" 2>&1 &
	pids+=($!)
	trap 'kill "${pids[@]}" 2>/dev/null || true' EXIT

	for _ in $(seq 1 180); do
		grep -q "and listening on port" "$logs/map.log" 2>/dev/null && break
		sleep 2
	done
	sleep 3
}

stop_servers() {
	kill "${pids[@]}" 2>/dev/null || true
	wait 2>/dev/null || true
	trap - EXIT
}

capture() {
	local mode="$1" out_dir="$reimagined/map-rules/tests/golden"
	build_servers "$mode"
	start_servers "$mode" "$mode" tools/golden/golden_driver.txt

	echo "== $mode: driving"
	python3 tools/ci/smoke_client.py --register --user golden1 --password goldenpw1 --char Golden \
		--stay 1800 --until-file "$logs/map.log" --until-text "GOLDEN DONE"
	stop_servers
	if ! grep -q "GOLDEN DONE" "$logs/map.log"; then
		echo "capture.sh: the driver did not finish (see $logs/map.log)" >&2
		exit 1
	fi

	# One file per kind, de-duplicated and sorted so re-captures diff cleanly.
	mkdir -p "$out_dir"
	python3 - "$trace" "$out_dir" "$mode" <<'EOF'
import json, sys, collections
trace, out_dir, mode = sys.argv[1:]
by_kind = collections.defaultdict(set)
# The drop and after-cast delay sweeps run last. They change jobs, stats, equipment and buffs, and
# fight, so the traces of the earlier sweeps (ASPD, defense) would depend on their timing: once the
# first record of a late sweep is seen, only the late sweeps' own kinds are kept.
LATE_SWEEPS = {"skull_drop", "nightmare_drop", "skill_delay"}
late = False
for line in open(trace):
    rec = json.loads(line)
    kind = rec.pop("kind")
    late = late or kind in LATE_SWEEPS
    if late and kind not in LATE_SWEEPS:
        continue
    by_kind[kind].add(json.dumps(rec, sort_keys=True))
for kind, rows in by_kind.items():
    name = f"{kind}_{'pre_re' if mode == 'prere' else 're'}.jsonl"
    with open(f"{out_dir}/{name}", "w") as f:
        f.write("".join(r + "\n" for r in sorted(rows)))
    print(f"   {name}: {len(rows)} vectors")
EOF
}

capture_chardel() {
	local out_dir="$reimagined/persistence/tests/golden"
	build_servers re # incremental when the re capture already built it
	start_servers re chardel tools/golden/golden_chardel.txt \
		"$(printf 'char_del_delay: 0\nchar_del_restriction: 0\nchar_del_level: 0\nchar_del_option: 2\n')"

	echo "== chardel: setting up the character"
	local out
	out="$(python3 tools/ci/smoke_client.py --register --user golden1 --password goldenpw1 --char Golden \
		--stay 120 --until-file "$logs/map.log" --until-text "GOLDEN SETUP DONE")"
	echo "$out"
	local cid
	cid="$(sed -n 's/^char: created char_id=\([0-9]*\).*/\1/p' <<< "$out")"
	sleep 3 # the map server saves the character on quit
	echo "== chardel: deleting char $cid"
	python3 tools/ci/smoke_client.py --user golden1 --password goldenpw1 --delete "$cid"
	sleep 1
	stop_servers

	# Keep the statements in execution order (the order is what the test pins).
	mkdir -p "$out_dir"
	python3 - "$trace" "$out_dir" <<'EOF'
import json, sys
trace, out_dir = sys.argv[1:]
rows = [json.loads(l) for l in open(trace)]
rows = sorted((r for r in rows if r["kind"] == "char_delete_sql"), key=lambda r: r["seq"])
with open(f"{out_dir}/char_delete_sql.jsonl", "w") as f:
    for i, r in enumerate(rows):
        f.write(json.dumps({"i": i, "sql": r["text"]}) + "\n")
print(f"   char_delete_sql.jsonl: {len(rows)} statements")
if not rows:
    sys.exit("capture.sh: no statements recorded for the delete")
EOF
}

capture_rng() {
	local dir="$root/build-golden/rng"
	mkdir -p "$dir" "$reimagined/core/tests/golden"
	"${CXX:-c++}" -std=c++17 -O1 -I"$root/src" -I"$root/src/common" \
		"$root/tools/golden/rng_capture.cpp" "$root/src/common/random.cpp" -o "$dir/rng_capture"
	"$dir/rng_capture" > "$reimagined/core/tests/golden/rng.jsonl"
	echo "   rng.jsonl: $(wc -l < "$reimagined/core/tests/golden/rng.jsonl") draws"
}

for m in $modes; do
	case "$m" in
		rng) capture_rng ;;
		chardel) capture_chardel ;;
		re|prere) capture "$m" ;;
		*) echo "capture.sh: unknown mode $m" >&2; exit 2 ;;
	esac
done
