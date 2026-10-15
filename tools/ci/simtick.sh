#!/usr/bin/env bash
# Measures the map server's sim tick (main-loop iteration time) under a bot load.
#
#   tools/ci/simtick.sh [options] [-- extra tools/ci/loadgen.py options]
#
#   --ccu "50 250 500"   bot counts to measure, one fresh server set and database each (default 50)
#   --ramp S             seconds over which the bots log in (default: ccu/10, at least 10)
#   --settle S           seconds between the end of the ramp and the measured phase (default 15)
#   --duration S         seconds of the measured phase (default 60)
#   --max-p99-ms MS      fail when the measured p99 exceeds this (default 50, the NFR)
#   --map NAME           map every bot is warped to on login (default prontera)
#   --crowd R            warp the bots to random walkable cells within R cells of --center instead
#                        of anywhere on the map (a crowd: every bot sees most of the others)
#   --center X,Y         centre of the crowd (default 156,191, Prontera's central square)
#   --procs N           loadgen client processes (default: 1 per 250 bots, at most nproc)
#   --prere              measure a pre-renewal build instead of renewal
#   --bin DIR            use the login/char/map binaries in DIR instead of building them
#   --out FILE           append one JSON line per CCU level with the measured numbers
#
# Builds Release servers into build-simtick/<re|prere>/ (the binaries in the repository root are
# not touched), runs each level in build-simtick/run-<mode>-<ccu>/ on a private configuration --
# the shipped conf/ files with a private conf/import/ that enables auto-registration, lifts the
# registration rate limit and allow-lists 127.0.0.1 past the DDoS guard -- and a private SQLite
# database. The map server runs with RATHENA_TICKSTATS (src/common/tickstats.hpp); the window
# histograms written during the measured phase are merged into p50/p95/p99/max.
#
# Needs: cmake, a C++ compiler, python3; nothing else may be listening on ports 6900/6121/5121.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"

ccus="50" ramp="" settle=15 duration=60 max_p99=50 map=prontera crowd=0 center=156,191 procs="" mode=re bin="" out=""
while [ $# -gt 0 ]; do
	case "$1" in
		--ccu) ccus="$2"; shift 2 ;;
		--ramp) ramp="$2"; shift 2 ;;
		--settle) settle="$2"; shift 2 ;;
		--duration) duration="$2"; shift 2 ;;
		--max-p99-ms) max_p99="$2"; shift 2 ;;
		--map) map="$2"; shift 2 ;;
		--crowd) crowd="$2"; shift 2 ;;
		--center) center="$2"; shift 2 ;;
		--procs) procs="$2"; shift 2 ;;
		--prere) mode=prere; shift ;;
		--bin) bin="$(cd "$2" && pwd)"; shift 2 ;;
		--out) out="$2"; shift 2 ;;
		--) shift; break ;;
		*) echo "simtick.sh: unknown option $1" >&2; exit 2 ;;
	esac
done
extra=("$@")
window=5 # seconds per tick-stats window

if [ -z "$bin" ]; then
	dir="$root/build-simtick/$mode"
	flags=""
	[ "$mode" = "prere" ] && flags="-DPRERE"
	echo "== building $mode servers (Release) in $dir"
	mkdir -p "$dir"
	cmake -S "$root" -B "$dir" -DCMAKE_BUILD_TYPE=Release -DWITH_POSTGRESQL=OFF \
		-DCMAKE_CXX_FLAGS="$flags" \
		-DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$dir/bin" \
		-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY="$dir/lib" \
		-DCMAKE_LIBRARY_OUTPUT_DIRECTORY="$dir/lib" > "$dir.configure.log"
	cmake --build "$dir" --parallel "${SIMTICK_JOBS:-4}" \
		--target login-server char-server map-server > "$dir.build.log"
	bin="$dir/bin"
fi

pids=()
stop_servers() {
	# SIGTERM: the servers shut down cleanly and the map server logs its whole-run summary.
	for p in "${pids[@]}"; do kill "$p" 2>/dev/null || true; done
	for p in "${pids[@]}"; do wait "$p" 2>/dev/null || true; done
	pids=()
}
trap stop_servers EXIT

# prepare_run <run dir>: private conf/import, database and logs; everything else links to the
# repository (the servers chdir to the directory of argv[0] and read conf/, db/, npc/ from there).
prepare_run() {
	local run="$1"
	rm -rf "$run"
	mkdir -p "$run/conf/import" "$run/save" "$run/log" "$run/logs"
	for f in "$root"/conf/*; do
		[ "$(basename "$f")" = "import" ] || ln -s "$f" "$run/conf/$(basename "$f")"
	done
	for d in db npc sql-files; do ln -s "$root/$d" "$run/$d"; done
	for f in "$root"/conf/import-tmpl/*.txt; do : > "$run/conf/import/$(basename "$f")"; done
	[ -e "$root/data" ] && ln -s "$root/data" "$run/data"
	printf 'db_backend: sqlite\nsqlite_directory: save\n' > "$run/conf/import/inter_conf.txt"
	printf 'new_account: yes\nallowed_regs: 1000000\ntime_allowed: 1\n' > "$run/conf/import/login_conf.txt"
	printf 'pincode_enabled: no\n' > "$run/conf/import/char_conf.txt"
	printf 'npc: %s\n' "$run/simtick_npc.txt" > "$run/conf/import/map_conf.txt"
	printf 'allow: 127.0.0.1\nddos_count: 1000000\n' > "$run/conf/import/packet_conf.txt"
	# Every player is warped on login to a random walkable cell of the benchmark map (or of the
	# crowd square with --crowd).
	local cx="${center%,*}" cy="${center#*,}"
	{
		printf -- '-\tscript\tsimtick_spread\t-1,{\nOnPCLoginEvent:\n'
		if [ "$crowd" -gt 0 ]; then
			printf '\tdo {\n\t\t.@x = rand(%d,%d);\n\t\t.@y = rand(%d,%d);\n' $((cx - crowd)) $((cx + crowd)) $((cy - crowd)) $((cy + crowd))
			printf '\t} while (!checkcell("%s",.@x,.@y,cell_chkpass));\n\twarp "%s",.@x,.@y;\n' "$map" "$map"
		else
			printf '\twarp "%s",0,0;\n' "$map"
		fi
		printf '\tend;\n}\n'
	} > "$run/simtick_npc.txt"
}

results=()
status=0
for ccu in $ccus; do
	scenario="spread"
	[ "$crowd" -gt 0 ] && scenario="crowd$crowd"
	run="$root/build-simtick/run-$mode-$scenario-$ccu"
	prepare_run "$run"
	stats="$run/tickstats.jsonl"
	r="${ramp:-$(( ccu / 10 > 10 ? ccu / 10 : 10 ))}"
	p="${procs:-$(( (ccu + 249) / 250 ))}"
	n="$(nproc 2>/dev/null || echo 4)"
	[ "$p" -gt "$n" ] && p="$n"

	echo "== $mode, $ccu CCU: starting servers in $run"
	(exec -a "$run/login-server" "$bin/login-server") > "$run/logs/login.log" 2>&1 &
	pids+=($!)
	(exec -a "$run/char-server" "$bin/char-server") > "$run/logs/char.log" 2>&1 &
	pids+=($!)
	(RATHENA_TICKSTATS=$window RATHENA_TICKSTATS_FILE="$stats" exec -a "$run/map-server" "$bin/map-server") > "$run/logs/map.log" 2>&1 &
	pids+=($!)
	for _ in $(seq 1 180); do
		grep -q "and listening on port" "$run/logs/map.log" 2>/dev/null && break
		sleep 2
	done
	sleep 3

	echo "== $mode, $ccu CCU: $ccu bots, ramp ${r}s, settle ${settle}s, measured ${duration}s, $p client process(es)"
	lg_status=0
	python3 "$root/tools/ci/loadgen.py" --bots "$ccu" --ramp "$r" --settle "$settle" --duration "$duration" \
		--procs "$p" "${extra[@]}" > "$run/logs/loadgen.log" 2>&1 || lg_status=$?
	grep -v "^loadgen: t+" "$run/logs/loadgen.log" || true
	# Let the map server finish logging the bots out (and saving them) before shutting it down.
	for _ in $(seq 1 30); do
		tail -n 1 "$stats" 2>/dev/null | grep -q '"users":0,' && break
		sleep 2
	done
	stop_servers

	line="$(python3 - "$stats" "$run/logs/loadgen.log" "$ccu" "$mode" "$max_p99" "$window" "$lg_status" "$scenario" <<'EOF'
import json, sys
stats, lglog, ccu, mode, max_p99, window, lg_status, scenario = sys.argv[1:]
steady = steady_end = None
summary = {}
for l in open(lglog):
    if l.startswith("STEADY "): steady = float(l.split()[1])
    elif l.startswith("STEADY_END "): steady_end = float(l.split()[1])
    elif l.startswith("SUMMARY "): summary = json.loads(l[8:])
try:
    recs = [json.loads(l) for l in open(stats)]
except OSError:
    recs = []
final = next((r for r in recs if r["final"]), None)
# windows entirely inside the measured phase (a record's time is the end of its window)
sel = [r for r in recs if not r["final"] and steady_end and steady + float(window) <= r["time"] <= steady_end]
buckets, total, mx, busy, secs = {}, 0, 0, 0, 0.0
for r in sel:
    for lo, hi, c in r["buckets"]:
        buckets[(lo, hi)] = buckets.get((lo, hi), 0) + c
    total += r["iterations"]; mx = max(mx, r["max_us"]); busy += r["busy_us"]; secs += r["seconds"]
def pct(p):
    if not total: return 0
    rank, seen = max(1, int(p * total + 0.999999)), 0
    for (lo, hi), c in sorted(buckets.items()):
        seen += c
        if seen >= rank: return min(hi, mx)
    return mx
users = [r["users"] for r in sel]
res = {
    "mode": mode, "scenario": scenario, "ccu": int(ccu), "bots_in": summary.get("ok", 0), "windows": len(sel),
    "users_min": min(users) if users else 0, "users_max": max(users) if users else 0,
    "iterations": total, "iter_per_s": round(total / secs, 1) if secs else 0,
    "p50_ms": pct(.50) / 1000, "p95_ms": pct(.95) / 1000, "p99_ms": pct(.99) / 1000, "max_ms": mx / 1000,
    "busy_pct": round(100 * busy / 1e6 / secs, 1) if secs else 0,
    "run_p99_ms": final["p99_us"] / 1000 if final else None, "run_max_ms": final["max_us"] / 1000 if final else None,
    "loadgen": summary, "loadgen_status": int(lg_status), "max_p99_ms": float(max_p99),
}
# a run where bots failed to get in measured a lighter load than asked: not a pass
res["pass"] = bool(sel) and res["p99_ms"] <= float(max_p99) and res["users_min"] >= 0.95 * int(ccu)
print(json.dumps(res))
EOF
)"
	echo "$line" > "$run/result.json"
	[ -n "$out" ] && echo "$line" >> "$out"
	results+=("$line")
	if ! python3 -c 'import json,sys; sys.exit(0 if json.loads(sys.argv[1])["pass"] else 1)' "$line"; then
		status=1
	fi
done

echo
echo "== sim tick, $mode build, $scenario, $(nproc 2>/dev/null) CPUs ($(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ *//'))"
printf '%-6s %-9s %-8s %-8s %-8s %-8s %-8s %-7s %-10s %-7s %-14s %s\n' CCU online iter/s p50_ms p95_ms p99_ms max_ms busy% run_p99_ms verdict client_lag_p99 failures
for line in "${results[@]}"; do
	python3 - "$line" <<'EOF'
import json, sys
r = json.loads(sys.argv[1]); lg = r["loadgen"]
online = f'{r["users_min"]}-{r["users_max"]}'
print(f'{r["ccu"]:<6} {online:<9} {r["iter_per_s"]:<8} {r["p50_ms"]:<8.3f} {r["p95_ms"]:<8.3f} {r["p99_ms"]:<8.3f} {r["max_ms"]:<8.3f} '
      f'{r["busy_pct"]:<7} {r["run_p99_ms"] or 0:<10.3f} {"PASS" if r["pass"] else "FAIL":<7} {lg.get("lag_p99_ms", 0):<14.1f} {lg.get("failed", {})}')
EOF
done
echo "(threshold: p99 <= $max_p99 ms over the measured phase; per-level JSON in build-simtick/run-$mode-$scenario-<ccu>/result.json; run_p99 includes the login ramp)"
exit $status
