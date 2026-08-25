#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$script_dir/common.sh"
root="$(repo_root)"
load_versions
strict=0
[ "${1:-}" = "--strict" ] && strict=1

[ -f "$root/build/cvisetup.sh" ] || die "run from the AirLink source tree"
[ ! -e "$root/.gitmodules" ] || die "top-level Git submodules are not allowed"
[ ! -d "$root/tools/airlink_recovery" ] || die "historical tools/airlink_recovery must not be published"
python3 "$root/scripts/verify_source_tree.py"

# shellcheck disable=SC1091
os_id="$(. /etc/os-release; printf '%s' "$ID")"
# shellcheck disable=SC1091
os_ver="$(. /etc/os-release; printf '%s' "$VERSION_ID")"
[ "$os_id" = ubuntu ] || die "Ubuntu is required (detected $os_id)"
[ "$os_ver" = 22.04 ] || die "Ubuntu 22.04 is required (detected $os_ver)"
grep -Eqi 'microsoft|wsl' /proc/version || printf 'NOTICE: native Ubuntu detected; WSL2 is the documented path.\n'

mem_kib="$(awk '/MemTotal:/ {print $2}' /proc/meminfo)"
disk_kib="$(df -Pk "$root" | awk 'NR==2 {print $4}')"
[ "$mem_kib" -ge 7500000 ] || die "at least 8 GB RAM is required"
[ "$disk_kib" -ge 83886080 ] || die "at least 80 GB free disk space is required"

path_scan="$(mktemp)"
trap 'rm -f -- "$path_scan"' EXIT
grep -RIn --exclude-dir=.git --exclude-dir=out --exclude='*.md' \
    --exclude=doctor.sh --exclude=verify_source_tree.py \
    '/home/pr/\|C:\\Users\\70918\|/mnt/c/Users/70918' \
    "$root/airlink" "$root/scripts" "$root/Makefile" >"$path_scan" || true
[ ! -s "$path_scan" ] || {
    cat "$path_scan" >&2
    die "developer-machine absolute path remains"
}
rm -f -- "$path_scan"
trap - EXIT

actual_vh="$(sha256sum "$root/airlink/rootfs/usr/bin/vhusbdriscv64" | awk '{print $1}')"
[ "$actual_vh" = "$VIRTUALHERE_RISCV64_SHA256" ] ||
    die "VirtualHere binary SHA256 mismatch"

if [ -L "$root/host-tools" ]; then
    die "host-tools must not be a symlink to another development tree"
fi

if [ "$strict" = 1 ]; then
    for tool in bash bc bison cmake cpio dtc fakeroot fdisk file flex gcc git jq         make mcopy mkfs.ext4 mkfs.vfat python3 readelf rsync sfdisk sha256sum         shellcheck strings time xz zstd; do
        need "$tool"
    done
    [ -x "$root/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-gcc" ] ||
        die "host-tools is missing; run make bootstrap"
    [ -f "$root/host-tools/.airlink-archive.sha256" ] ||
        die "host-tools archive marker is missing; rerun make bootstrap"
    [ "$(cat "$root/host-tools/.airlink-archive.sha256")" = "$HOST_TOOLS_SHA256" ] ||
        die "host-tools archive marker does not match versions.lock"
    need riscv64-unknown-elf-gcc
fi

printf 'AirLink doctor: PASS\n'
printf '  Ubuntu: %s\n  RAM: %s MiB\n  free disk: %s GiB\n'     "$os_ver" "$((mem_kib / 1024))" "$((disk_kib / 1024 / 1024))"
