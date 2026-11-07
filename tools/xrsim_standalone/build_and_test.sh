#!/usr/bin/env bash
# Build xrSimCore + its unit tests without the engine (Linux/macOS, any compiler with C++20).
# Usage: tools/xrsim_standalone/build_and_test.sh [build-dir]
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="${1:-$ROOT/build-xrsim}"
GEN=()
command -v ninja >/dev/null 2>&1 && GEN=(-G Ninja)
cmake -S "$HERE" -B "$OUT" "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release >"$OUT.cmake.log" 2>&1 || { cat "$OUT.cmake.log"; exit 1; }
cmake --build "$OUT" --parallel
cd "$ROOT"
for t in xrSimWorldStateTests xrSimAuthoritySpineTests; do
  echo "== $t"; "$OUT/$t"; echo "   exit=$?"
done
