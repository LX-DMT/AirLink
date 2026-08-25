#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$script_dir/common.sh"
root="$(repo_root)"
load_versions
out="$root/out/components"

rm -rf -- "$out"
mkdir -p "$out/c906l" "$out/linux"

"$root/airlink/c906l/build.sh" "$out/c906l"
"$root/airlink/linux/build_linux.sh" "$out/linux"
python3 "$root/airlink/linux/test_airlinkd_static.py"
python3 "$root/airlink/linux/test_peer_firmware_contract.py"
python3 "$root/airlink/c906l/test_protocol.py"

strings "$out/c906l/r26-lvgl.bin" | grep -F 'START proto=1 abi=4' >/dev/null ||
    die "C906L ABI4 marker missing"
strings "$out/linux/airlinkd" | grep -F 'firmware=LN27' >/dev/null ||
    die "airlinkd LN27 marker missing"

sha256sum "$out/c906l/r26-lvgl.bin" "$out/linux/airlinkd"     "$out/linux/airlinkctl" > "$out/SHA256SUMS"
printf 'AirLink components: PASS\n'
