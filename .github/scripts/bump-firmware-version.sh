#!/usr/bin/env bash
# Usage: bump-firmware-version.sh <MAJOR.MINOR.PATCH> [path/to/config.h]
#
# Rewrites the default `#define FIRMWARE_VERSION "..."` in config.h to the given version.
# Exit 0: file changed. Exit 3: nothing to do (already that version or newer). Exit 1: error.
# The newer-or-equal skip makes re-runs and out-of-order tags harmless.
set -euo pipefail

VERSION="${1:?version required}"
CONFIG="${2:-firmware/src/config.h}"

if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "::error::'$VERSION' is not MAJOR.MINOR.PATCH" >&2
  exit 1
fi

PATTERN='^#define FIRMWARE_VERSION "[0-9]+\.[0-9]+\.[0-9]+"$'
if [ "$(grep -cE "$PATTERN" "$CONFIG")" != 1 ]; then
  echo "::error::expected exactly one '#define FIRMWARE_VERSION \"x.y.z\"' line in $CONFIG" >&2
  exit 1
fi

CURRENT="$(grep -E "$PATTERN" "$CONFIG" | sed -E 's/.*"([^"]+)".*/\1/')"
if [ "$(printf '%s\n%s\n' "$CURRENT" "$VERSION" | sort -V | tail -n1)" = "$CURRENT" ]; then
  echo "config.h already at $CURRENT (>= $VERSION), nothing to do"
  exit 3
fi

TMP="$(mktemp)"
sed -E "s/^(#define FIRMWARE_VERSION \")[0-9]+\.[0-9]+\.[0-9]+(\")$/\1${VERSION}\2/" "$CONFIG" > "$TMP"
cat "$TMP" > "$CONFIG"
rm -f "$TMP"

if ! grep -qx "#define FIRMWARE_VERSION \"$VERSION\"" "$CONFIG"; then
  echo "::error::edit of $CONFIG did not produce the expected line" >&2
  exit 1
fi
echo "config.h: $CURRENT -> $VERSION"
