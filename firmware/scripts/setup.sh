#!/usr/bin/env bash
# Prepare the PapyriX submodule for a margin build:
#   1. check out the pinned upstream commit,
#   2. apply margin's hook patches (firmware/patches/*.patch),
#   3. link platformio.margin.ini in as platformio.local.ini.
# Idempotent: re-running resets the submodule to the pin and re-applies the patches.
set -euo pipefail

FW="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$(cd "$FW/.." && pwd)"
UP="$FW/upstream/papyrix-reader"

git -C "$ROOT" submodule update --init firmware/upstream/papyrix-reader
# Throw away local edits inside the submodule (patches are the source of truth).
git -C "$UP" reset --hard --quiet
git -C "$UP" clean -fdq -e platformio.local.ini

for p in "$FW"/patches/*.patch; do
  [ -e "$p" ] || continue
  echo "applying $(basename "$p")"
  git -C "$UP" apply --whitespace=nowarn "$p"
done

ln -sf ../../platformio.margin.ini "$UP/platformio.local.ini"
grep -qxF 'platformio.local.ini' "$(git -C "$UP" rev-parse --git-path info/exclude)" 2>/dev/null ||
  echo 'platformio.local.ini' >> "$(git -C "$UP" rev-parse --git-path info/exclude)"

echo "ready: cd firmware/upstream/papyrix-reader && pio run -e margin_x4pro"
