#!/usr/bin/env bash
# End-to-end smoke test of the login, char and map servers on one database backend.
#
#   tools/ci/smoke.sh sqlite
#   RATHENA_PG="host:port:user:password:dbname" tools/ci/smoke.sh postgresql
#
# Expects the server binaries in the repository root. The database must be empty or hold the
# rAthena schema; the servers create the schema on first start (db_auto_create_schema).
# Fails when any step of tools/ci/smoke_client.py fails or a server logs an SQL error.
set -euo pipefail

backend="${1:-sqlite}"
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
logs="${SMOKE_LOG_DIR:-$root/smoke-logs}"
mkdir -p "$logs" conf/import db/import
for f in conf/import-tmpl/*; do [ -e "conf/import/$(basename "$f")" ] || cp "$f" conf/import/; done
for f in db/import-tmpl/*; do [ -e "db/import/$(basename "$f")" ] || cp -r "$f" db/import/; done

echo "pincode_enabled: no" > conf/import/char_conf.txt
echo "new_account: yes" > conf/import/login_conf.txt

if [ "$backend" = "postgresql" ]; then
	IFS=: read -r pg_host pg_port pg_user pg_pass pg_db <<< "${RATHENA_PG:?set RATHENA_PG=host:port:user:password:dbname}"
	{
		echo "db_backend: postgresql"
		for k in login_server ipban_db char_server map_server web_server log_db; do
			echo "${k}_ip: $pg_host"
			echo "${k}_port: $pg_port"
			echo "${k}_id: $pg_user"
			echo "${k}_pw: $pg_pass"
			echo "${k}_db: $pg_db"
		done
	} > conf/import/inter_conf.txt
else
	echo "db_backend: sqlite" > conf/import/inter_conf.txt
	rm -f save/*.db save/*.db-wal save/*.db-shm
fi

pids=()
cleanup() {
	for p in "${pids[@]}"; do kill "$p" 2>/dev/null || true; done
	wait 2>/dev/null || true
}
trap cleanup EXIT

for s in login char map; do
	"./$s-server" > "$logs/$s-$backend.log" 2>&1 &
	pids+=($!)
done
for _ in $(seq 1 120); do
	grep -q "and listening on port" "$logs/map-$backend.log" 2>/dev/null && break
	sleep 2
done
sleep 3

python3 tools/ci/smoke_client.py --register --user smoke1 --password smokepw1 --char Smokey
python3 tools/ci/smoke_client.py --user smoke1 --password smokepw1 --no-create

cleanup
pids=()
if grep -n -A1 "DB error\|SQL error\|\[SQL\]: .*error" "$logs"/*-"$backend".log; then
	echo "smoke: SQL errors in the server logs" >&2
	exit 1
fi
echo "smoke: $backend ok"
