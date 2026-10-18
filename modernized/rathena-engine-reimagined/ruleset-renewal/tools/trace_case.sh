#!/usr/bin/env bash
# Development aid: tools/trace_case.sh <case-id> [luau]   prints one golden case and the Luau replay with both stage traces.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 -I tools/bundle_luau.py tests/trace_case build/bundle_trace.luau >/dev/null
sed -i "2i RE_CASE = $1" build/bundle_trace.luau
"${2:-${LUAU:-luau}}" build/bundle_trace.luau
