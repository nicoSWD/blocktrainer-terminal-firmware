#!/usr/bin/env bash
# Fetch the OTA firmware from blocktrainer.de (HTTPS).
# Downloads firmware.json (manifest) + the three flash images.
set -euo pipefail

BASE="https://btclock.blocktrainer.de/v2/firmware"

for f in firmware.json bootloader.bin partitions.bin firmware.bin; do
  curl -sSL -o "$f" "$BASE/$f"
  echo "$f  $(du -h "$f" | cut -f1)"
done

echo
echo "Done. firmware.json lists the parts + offsets; firmware.bin is the app image."
