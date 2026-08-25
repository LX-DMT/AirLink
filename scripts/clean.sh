#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [ "$root" = / ] || [ ! -f "$root/versions.lock" ]; then
    echo "ERROR: unsafe clean root" >&2
    exit 1
fi
rm -rf -- "$root/out"
printf 'Removed %s/out\n' "$root"
