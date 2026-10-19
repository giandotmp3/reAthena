#!/usr/bin/env bash
# Development aid: tools/status_trace.sh <case-id> [luau]   prints one status golden case, the legacy output, the Luau replay and the contributions.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 -I tools/build_golden.py >/dev/null
python3 -I tools/bundle_luau.py tests/status_trace build/bundle_trace.luau >/dev/null
sed -i "2i RE_CASE = $1" build/bundle_trace.luau
"${2:-${LUAU:-luau}}" build/bundle_trace.luau
