#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$script_dir/common.sh"
root="$(repo_root)"
log_dir="$root/out/logs"
release_dir="$root/out/release"
log="$log_dir/release-build.log"
resource="$release_dir/build-resource.txt"

mkdir -p "$log_dir"
rm -rf -- "$release_dir"
mkdir -p "$release_dir"
rm -f -- "$log"

set +e
/usr/bin/time -v -o "$resource" "$script_dir/release_inner.sh" 2>&1 | tee "$log"
rc=${PIPESTATUS[0]}
set -e

if [ -f "$log" ]; then
    zstd -T0 -10 -f "$log" -o "$release_dir/build.log.zst" >/dev/null
fi
[ "$rc" -eq 0 ] || exit "$rc"

"$script_dir/generate_validation_evidence.sh"
printf 'AirLink complete release build: PASS\n'
