#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$script_dir/../.." && pwd)"
out="${1:-$script_dir/linux-out}"
cross="${LINUX_CROSS_COMPILE:-$repo/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-}"
cc="${cross}gcc"
command -v "$cc" >/dev/null 2>&1 || { echo "ERROR missing $cc" >&2; exit 1; }
command -v gcc >/dev/null 2>&1 || { echo "ERROR missing host gcc" >&2; exit 1; }
rm -rf -- "$out"
mkdir -p "$out/generated"
portal="$script_dir/../portal/index.html"
python3 "$script_dir/test_portal_ui.py"
python3 "$script_dir/test_virtualhere_ready.py"
python3 "$script_dir/embed_portal.py" "$portal"     "$out/generated/airlink_portal.inc"
common=(-std=c11 -Os -static -ffunction-sections -fdata-sections -fno-common -Wall -Wextra -Werror '-Wl,--gc-sections' -I"$script_dir/../ipc" -I"$out/generated")
"$cc" "${common[@]}" "$script_dir/airlinkd.c" "$script_dir/airlink_provision.c" -o "$out/airlinkd"
"$cc" "${common[@]}" "$script_dir/airlinkctl.c" -o "$out/airlinkctl"
gcc -std=c11 -O2 -Wall -Wextra -Werror -I"$script_dir/../ipc" -I"$out/generated" "$script_dir/airlinkd.c" "$script_dir/airlink_provision.c" -o "$out/airlinkd-host-selftest"
"$out/airlinkd-host-selftest" --protocol-selftest
python3 "$script_dir/test_r26_runtime.py" "$out/airlinkd-host-selftest"
rm -f "$out/airlinkd-host-selftest"
file "$out/airlinkd" "$out/airlinkctl"
if readelf -d "$out/airlinkd" 2>/dev/null | grep -q NEEDED; then
    echo "ERROR airlinkd is not static" >&2
    exit 1
fi
if readelf -d "$out/airlinkctl" 2>/dev/null | grep -q NEEDED; then
    echo "ERROR airlinkctl is not static" >&2
    exit 1
fi
strings "$out/airlinkd" > "$out/airlinkd.strings"
grep -F 'firmware=LN27' "$out/airlinkd.strings" >/dev/null
grep -F '"version":"R27.6.6.22"' "$out/airlinkd.strings" >/dev/null
grep -F 'IPC peer=R27P firmware=0x%08x abi=%u PASS' "$out/airlinkd.strings" >/dev/null
grep -F 'IPC FAIL stage=CSTATE_FW expected=0x%08x actual=0x%08x' "$out/airlinkd.strings" >/dev/null
grep -F 'SELFTEST PASS pings=3 abi=4' "$out/airlinkd.strings" >/dev/null
grep -F '12345678' "$out/airlinkd.strings" >/dev/null
grep -F 'STOP complete; managed services stopped' "$out/airlinkd.strings" >/dev/null
grep -F 'VH early-listener=DISABLED wait=ASSOCIATED+IP+ROUTE+2000MS' "$out/airlinkd.strings" >/dev/null
grep -F 'VH lan-activate begin ipv4=%s schedule_ms=0,1000,2000,4000,8000,16000,30000 maintenance_ms=%u' "$out/airlinkd.strings" >/dev/null
grep -F 'VH lan-activate step=%u phase=%s ipv4=%s garp=%u/2 udp_broadcast=%u/1 client=WAIT' "$out/airlinkd.strings" >/dev/null
grep -F 'VH lan-activate client=CONNECTED ipv4=%s attempts=%u elapsed_ms=%llu PASS' "$out/airlinkd.strings" >/dev/null
grep -F 'WIFI AP-to-STA datapath-reset down_ms=%u up_settle_ms=%u elapsed_ms=%llu PASS' "$out/airlinkd.strings" >/dev/null
grep -F 'WIFI power-save requested=OFF actual=%s stage=%s %s' "$out/airlinkd.strings" >/dev/null
grep -F 'VH client-state=CONNECTED ip=%s stable_ms=%u' "$out/airlinkd.strings" >/dev/null
grep -F 'VH client-state=DISCONNECTED stable_ms=%u' "$out/airlinkd.strings" >/dev/null
grep -F 'check-pc-network-or-ap-isolation' "$out/airlinkd.strings" >/dev/null
grep -F 'SDIO requested=%u actual=%u timing=%u PASS' "$out/airlinkd.strings" >/dev/null
rm -f "$out/airlinkd.strings"
sha256sum "$out/airlinkd" "$out/airlinkctl" > "$out/sha256.txt"
echo "R27.6.6.22 Linux binaries built in $out"
