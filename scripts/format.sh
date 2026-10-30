#!/usr/bin/env bash
# Format the firmware sources with clang-format (config: firmware/.clang-format).
#   scripts/format.sh           rewrite files in place
#   scripts/format.sh --check   fail if any file would change (used by CI)
set -euo pipefail

cd "$(dirname "$0")/../firmware"

CF="${CLANG_FORMAT:-}"
if [ -z "$CF" ]; then
    if [ -x .venv/bin/clang-format ]; then CF=.venv/bin/clang-format; else CF=clang-format; fi
fi

files=(src/*.cpp src/*.h host/*.cpp host/*.h)

if [ "${1:-}" = "--check" ]; then
    exec "$CF" --dry-run -Werror "${files[@]}"
fi
exec "$CF" -i "${files[@]}"
