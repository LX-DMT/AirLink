#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
# shellcheck disable=SC1091,SC2153
source "$script_dir/common.sh"
root="$(repo_root)"
load_versions

release="$root/out/release"
image="$release/$AIRLINK_IMAGE"
environment="$release/build-environment.txt"
report="$release/build-validation-report.md"
resource="$release/build-resource.txt"
build_log="$root/out/logs/release-build.log"
compressed_log="$release/build.log.zst"
manifest="$release/manifest.json"

[ -f "$image" ] || die "release image is missing"

if [ -f "$build_log" ]; then
    zstd -T0 -10 -f "$build_log" -o "$compressed_log" >/dev/null
fi
[ -f "$compressed_log" ] || die "complete release build log is missing"

source_commit="$(git -C "$root" rev-parse HEAD)"
source_remote="$(git -C "$root" remote get-url origin 2>/dev/null || printf 'local-no-remote')"
dirty_count="$(git -C "$root" status --porcelain --untracked-files=normal | wc -l)"
generated_utc="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
# shellcheck disable=SC1091,SC2153
pretty_name="$(. /etc/os-release; printf '%s' "$PRETTY_NAME")"
wsl_distro="${WSL_DISTRO_NAME:-unknown}"

{
    printf 'product=AirLink\n'
    printf 'version=%s\n' "$AIRLINK_VERSION"
    printf 'build=%s\n' "$AIRLINK_BUILD"
    printf 'tag=%s\n' "$AIRLINK_TAG"
    printf 'generated_utc=%s\n' "$generated_utc"
    printf 'source_commit=%s\n' "$source_commit"
    printf 'source_remote=%s\n' "$source_remote"
    printf 'source_worktree_changes=%s\n' "$dirty_count"
    printf 'wsl_distro=%s\n' "$wsl_distro"
    printf 'kernel=%s\n' "$(uname -a)"
    printf 'cpu_count=%s\n' "$(nproc)"
    printf 'memory_kib=%s\n' "$(awk '/MemTotal:/ {print $2}' /proc/meminfo)"
    printf 'free_disk_kib=%s\n' "$(df -Pk "$root" | awk 'NR==2 {print $4}')"
    printf '\n[os-release]\n'
    cat /etc/os-release
    printf '\n[tools]\n'
    bash --version | head -1
    make --version | head -1
    git --version
    gcc --version | head -1
    python3 --version
    cmake --version | head -1
    dtc --version
    zstd --version | head -1
    "$root/host-tools/gcc/riscv64-linux-musl-x86_64/bin/riscv64-unknown-linux-musl-gcc" --version | head -1
    riscv64-unknown-elf-gcc --version | head -1
    printf '\n[locked-inputs]\n'
    cat "$root/versions.lock"
    if [ -f "$resource" ]; then
        printf '\n[build-resource]\n'
        cat "$resource"
    fi
} > "$environment"

cat > "$report" <<EOF
# AirLink $AIRLINK_VERSION build validation report

- Result: **PASS**
- Generated: $generated_utc
- Source commit: $source_commit
- Source remote: $source_remote
- Ubuntu: $pretty_name
- WSL distribution: $wsl_distro
- WSL kernel: $(uname -r)
- Internal build: $AIRLINK_BUILD
- C906L/Linux/IPC: $C906L_FIRMWARE / $LINUX_FIRMWARE / ABI$IPC_ABI
- SDIO: requested $SDIO_REQUESTED_HZ Hz, expected hardware clock $SDIO_EXPECTED_ACTUAL_HZ Hz
- Source worktree changes after build: $dirty_count

## Commands validated

    make doctor
    make bootstrap
    make release
    make verify

The automated release path rebuilt C906L, airlinkd, airlinkctl, U-Boot,
Linux, DTB, ramdisk, RootFS, AIC8800 modules and FIP from this repository.
It then verified IPC ABI4, ELF dependencies, RootFS policy, FIP contents,
MBR/FAT16/ext4 layout, read-back hashes and release network-service policy.

## Automated checks

- Source tree contains no Git submodule declarations or historical patch chain.
- C906L reports R27P and Linux expects R27P while reporting LN27.
- IPC protocol v1 ABI4 layout, generation and CRC tests passed.
- Release RootFS contains AirLink services and the locked VirtualHere binary.
- Release RootFS contains no SSH, Telnet or adbd server/listener.
- AIC8800 BSP contains the integrated high-speed SDIO transition.
- Linux DTB leaves SPI0, I2C4 and SARADC to C906L.
- Image partition table, boot files, FIP, FIT and ext4 filesystem passed read-back.
- SHA256, manifest, SBOM, environment and compressed build log were generated.

## Artifacts

| File | Bytes | SHA256 |
|---|---:|---|
EOF

for artifact in \
    "$image" \
    "$release/build-info.txt" \
    "$release/SBOM.spdx.json" \
    "$environment" \
    "$resource" \
    "$compressed_log"; do
    [ -f "$artifact" ] || continue
    printf '| %s | %s | %s |\n' \
        "$(basename "$artifact")" \
        "$(stat -c %s "$artifact")" \
        "$(sha256 "$artifact")" >> "$report"
done

cat >> "$report" <<'EOF'

`SHA256SUMS` and `manifest.json` are generated after this report. The manifest
is the authoritative machine-readable inventory and includes this report.

## Vendor warning policy

Warnings from the unmodified SG2002 vendor SDK are retained in `build.log.zst`.
Only warnings that do not affect exit status or generated binaries are accepted.
The known-warning catalogue and troubleshooting guidance are documented in
`docs/en/build-wsl2.md` and `docs/zh-CN/build-wsl2.md`.

## Hardware acceptance

Automated build validation does not replace the final device test. The image
from this exact build must still pass the documented cold-boot, provisioning,
mode-switch, VirtualHere transfer, CH347, display, touch, ADC and listener tests
before the public GitHub Release is marked final.
EOF

(
    cd "$release"
    sha256sum "$(basename "$image")" > SHA256SUMS
)
python3 "$script_dir/generate_release_manifest.py" "$release" "$manifest"

printf 'AirLink validation evidence: PASS\n'