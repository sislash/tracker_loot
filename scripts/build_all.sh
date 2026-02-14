#!/usr/bin/env bash
set -euo pipefail

# Build helper for CI/local quick-check.
# Note: WERROR=0 avoids failing on compiler warning differences across GCC/Clang.

echo "[1/2] Linux release"
make release WERROR=0

if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
  echo "[2/2] Windows cross-compile"
  make win WERROR=0
else
  echo "[2/2] MinGW-w64 not found: skipping Windows build" >&2
fi
