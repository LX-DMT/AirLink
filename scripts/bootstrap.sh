#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$script_dir/common.sh"
root="$(repo_root)"
load_versions

if [ "$(id -u)" -eq 0 ]; then
    sudo_cmd=()
else
    sudo_cmd=(sudo)
fi

"${sudo_cmd[@]}" apt-get update
"${sudo_cmd[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y     android-sdk-libsparse-utils autoconf automake     bc bison build-essential cmake cpio curl device-tree-compiler dosfstools     e2fsprogs fakeroot fdisk file flex gawk gcc-riscv64-unknown-elf git jq     libelf-dev libglib2.0-dev libncurses-dev libpixman-1-dev libssl-dev     libtool mtools ninja-build openssh-client parallel pkg-config python-is-python3     python3 python3-distutils python3-pip rsync scons shellcheck squashfs-tools     tclsh texinfo time tree unzip util-linux wget xz-utils zstd

[ ! -L "$root/host-tools" ] || die "refusing an external host-tools symlink"
cache_dir="$root/.cache"
archive="$cache_dir/host-tools-$HOST_TOOLS_SHA256.tar.gz"
mkdir -p "$cache_dir"
if [ ! -e "$root/host-tools" ]; then
    if [ -f "$archive" ] && [ "$(sha256 "$archive")" != "$HOST_TOOLS_SHA256" ]; then
        printf 'Discarding incomplete host-tools archive.\n'
        rm -f -- "$archive"
    fi
    if [ ! -f "$archive" ]; then
        curl --fail --location --retry 20 --retry-all-errors --retry-delay 5 \
            --continue-at - --connect-timeout 20 \
            --output "$archive" "$HOST_TOOLS_URL"
    fi
    [ "$(sha256 "$archive")" = "$HOST_TOOLS_SHA256" ] ||
        die "host-tools archive SHA256 mismatch"
    tmp="$root/.host-tools.tmp.$$"
    trap 'rm -rf -- "$tmp"' EXIT
    mkdir -p "$tmp"
    tar -xzf "$archive" -C "$tmp"
    [ -d "$tmp/host-tools/gcc" ] ||
        die "host-tools archive layout is invalid"
    printf '%s\n' "$HOST_TOOLS_SHA256" >"$tmp/host-tools/.airlink-archive.sha256"
    mv "$tmp/host-tools" "$root/host-tools"
    rmdir "$tmp"
    trap - EXIT
fi
[ -f "$root/host-tools/.airlink-archive.sha256" ] ||
    die "host-tools exists but has no AirLink archive marker; remove it and rerun make bootstrap"
actual_host_tools_sha="$(cat "$root/host-tools/.airlink-archive.sha256")"
[ "$actual_host_tools_sha" = "$HOST_TOOLS_SHA256" ] ||
    die "host-tools archive mismatch: expected $HOST_TOOLS_SHA256, got $actual_host_tools_sha"
[ -x "$root/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-gcc" ] ||
    die "downloaded host-tools does not contain the expected RISC-V musl compiler"

"$script_dir/doctor.sh" --strict
printf 'AirLink bootstrap: PASS\n'
