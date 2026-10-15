#!/usr/bin/env bash
# Buildin corpus: runs the scripts in tools/ci/script_corpus/ through the real script VM, headless.
#
#   tools/ci/script_corpus.sh [<directory with map-server>]     (default: the repository root)
#
# Starts map-server --run-once on a private configuration and SQLite database under
# corpus-run/ (conf/import is left alone): the default map configuration plus one `npc:` line
# per corpus file. Every corpus NPC runs its checks in OnInit and prints "CORPUS PASS <test>"
# or "CORPUS FAIL <test> ..." (tools/ci/script_corpus/_lib.txt). The full NPC set of
# npc/scripts_main.conf loads as well, so the run doubles as a load check.
# Fails on any FAIL line, on any test without a PASS line (a script error ends the script before
# its PASS line) and when the map server does not exit cleanly.
# Prints the buildin coverage against doc/script_buildins.tsv at the end.
#
# The map server binds its port while it initialises; nothing else may be listening on 5121.
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
bin="$(cd "${1:-$root}" && pwd)"
run_dir="${CORPUS_RUN_DIR:-$root/corpus-run}"
corpus_dir="tools/ci/script_corpus"
conf="$run_dir/conf"
save="$run_dir/save"
log="$run_dir/map.log"

[ -x "$bin/map-server" ] || { echo "script_corpus: $bin/map-server not found" >&2; exit 2; }
mkdir -p conf/import db/import
for f in conf/import-tmpl/*; do [ -e "conf/import/$(basename "$f")" ] || cp "$f" conf/import/; done
for f in db/import-tmpl/*; do [ -e "db/import/$(basename "$f")" ] || cp -r "$f" db/import/; done

rm -rf "$save" "$log"
mkdir -p "$conf" "$save"
printf 'import: conf/inter_athena.conf\ndb_backend: sqlite\nsqlite_directory: %s\n' "$save" > "$conf/inter.conf"
{
	echo "import: conf/map_athena.conf"
	for f in $(LC_ALL=C ls "$corpus_dir"/*.txt); do echo "npc: $f"; done
} > "$conf/map.conf"

echo "script_corpus: running map-server --run-once (log: $log)"
# The servers chdir to the directory of argv[0]; name it as if it lived in the root.
status=0
(exec -a "$root/corpus-map-server" timeout 900 "$bin/map-server" --run-once \
	--map-config "$conf/map.conf" --inter-config "$conf/inter.conf") > "$log" 2>&1 || status=$?

# ANSI colour codes off, one line per message
sed -i 's/\x1b\[[0-9;]*m//g' "$log"

expected="$(grep -h '^[[:space:]]*callfunc "corpus_begin", "' "$corpus_dir"/*.txt | sed 's/.*"corpus_begin", "\([^"]*\)".*/\1/' | sort)"
passed="$(sed -n 's/.*CORPUS PASS \([^ ]*\).*/\1/p' "$log" | sort)"
fail=0

if grep -n "CORPUS FAIL" "$log"; then
	fail=1
fi
missing="$(comm -23 <(echo "$expected") <(echo "$passed"))"
if [ -n "$missing" ]; then
	echo "script_corpus: no PASS line for: $(echo $missing)" >&2
	fail=1
fi
if [ "$status" -ne 0 ]; then
	echo "script_corpus: map-server exited with status $status" >&2
	tail -n 30 "$log" >&2
	fail=1
fi

grep -h "CORPUS PASS" "$log" | sed 's/.*\(CORPUS PASS\)/\1/'
python3 tools/ci/script_corpus_coverage.py
if [ "$fail" -ne 0 ]; then
	echo "script_corpus: FAILED" >&2
	exit 1
fi
echo "script_corpus: $(echo "$expected" | wc -l) tests passed"
