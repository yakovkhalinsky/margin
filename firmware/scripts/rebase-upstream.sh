#!/usr/bin/env bash
# Move the PapyriX pin to a newer upstream commit and check that margin's patches still apply.
# Usage: firmware/scripts/rebase-upstream.sh [<commit-or-ref>]   (default: origin/main)
set -euo pipefail

FW="$(cd "$(dirname "$0")/.." && pwd)"
UP="$FW/upstream/papyrix-reader"
REF="${1:-origin/main}"

git -C "$UP" fetch --quiet origin
git -C "$UP" reset --hard --quiet
git -C "$UP" checkout --quiet --detach "$REF"
for p in "$FW"/patches/*.patch; do
  [ -e "$p" ] || continue
  if ! git -C "$UP" apply --check "$p"; then
    echo "patch no longer applies: $(basename "$p") - refresh it against $(git -C "$UP" rev-parse --short HEAD)" >&2
    exit 1
  fi
done
echo "pin moved to $(git -C "$UP" rev-parse --short HEAD); run scripts/setup.sh, build, test on device, then commit the submodule bump"
