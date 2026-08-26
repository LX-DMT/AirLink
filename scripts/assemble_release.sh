#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$script_dir/common.sh"
root="$(repo_root)"
load_versions

sdk="$root/install/soc_sg2002_licheervnano_sd"
sdk_rootfs="$root/buildroot/output/target"
components="$root/out/components"
release_dir="$root/out/release"
work="$root/out/work-release"
image_name="$AIRLINK_IMAGE"
image="$release_dir/$image_name"

for path in "$sdk/fip.bin" "$sdk/rawimages/boot.sd" "$sdk_rootfs/etc/init.d"     "$components/c906l/r26-lvgl.bin" "$components/linux/airlinkd"     "$components/linux/airlinkctl"; do
    [ -e "$path" ] || die "required build output missing: $path"
done

rm -rf -- "$work"
mkdir -p "$work/rootfs" "$work/fip" "$release_dir"
cp -a "$sdk_rootfs/." "$work/rootfs/"
rootfs_before_bytes="$(du -sb "$work/rootfs" | awk '{print $1}')"

install -D -m 0755 "$components/linux/airlinkd" "$work/rootfs/usr/sbin/airlinkd"
install -D -m 0755 "$components/linux/airlinkctl" "$work/rootfs/usr/sbin/airlinkctl"
install -D -m 0755 "$root/airlink/linux/S29airlinkd" "$work/rootfs/etc/init.d/S29airlinkd"
install -D -m 0755 "$root/airlink/rootfs/etc/init.d/S25wifimod" "$work/rootfs/etc/init.d/S25wifimod"
install -D -m 0755 "$root/airlink/rootfs/usr/bin/vhusbdriscv64"     "$work/rootfs/usr/bin/vhusbdriscv64"

for legacy in S04fb S05tp S30wifi S31wifi_config S80dnsmasq     S99vhusbdriscv64 S99zzz_airlinkui; do
    rm -f -- "$work/rootfs/etc/init.d/$legacy"
done
rm -f -- "$work/rootfs/boot/wifi.sta" "$work/rootfs/boot/wifi.ssid"     "$work/rootfs/boot/wifi.pass" "$work/rootfs/etc/wpa_supplicant.conf"     "$work/rootfs/data/airlink/wifi.conf"
rm -rf -- "$work/rootfs/run/airlink" "$work/rootfs/run/wpa_supplicant"
mkdir -p "$work/rootfs/data/airlink" "$work/rootfs/run/airlink"
chmod 0700 "$work/rootfs/data/airlink"

cat > "$work/rootfs/etc/airlink-release" <<EOF
product=$AIRLINK_PRODUCT
version=$AIRLINK_VERSION
build=$AIRLINK_BUILD
profile=release
c906l_firmware=$C906L_FIRMWARE
linux_firmware=$LINUX_FIRMWARE
ipc_protocol=$IPC_PROTOCOL
ipc_abi=$IPC_ABI
sdio_requested_hz=$SDIO_REQUESTED_HZ
sdio_expected_actual_hz=$SDIO_EXPECTED_ACTUAL_HZ
image_name=$image_name
EOF
chmod 0644 "$work/rootfs/etc/airlink-release"

python3 "$script_dir/prune_rootfs.py" "$work/rootfs" release     "$release_dir/deleted-files.txt"
python3 "$script_dir/verify_rootfs.py" "$work/rootfs" release     "$release_dir/elf-dependencies.txt"

rootfs_after_bytes="$(du -sb "$work/rootfs" | awk '{print $1}')"
rootfs_limit_bytes=$((120 * 1024 * 1024))
{
    printf 'AirLink %s (%s) Release RootFS size report\n' "$AIRLINK_VERSION" "$AIRLINK_BUILD"
    printf 'before_prune_bytes=%s\n' "$rootfs_before_bytes"
    printf 'after_prune_bytes=%s\n' "$rootfs_after_bytes"
    printf 'limit_bytes=%s\n' "$rootfs_limit_bytes"
    printf 'after_prune_mib=%s\n' "$(awk -v n="$rootfs_after_bytes" 'BEGIN {printf "%.2f", n / 1048576}')"
    printf 'result=%s\n' "$([ "$rootfs_after_bytes" -le "$rootfs_limit_bytes" ] && printf PASS || printf FAIL)"
} > "$release_dir/rootfs-size.txt"
[ "$rootfs_after_bytes" -le "$rootfs_limit_bytes" ] ||
    die "Release RootFS exceeds 120 MiB: $rootfs_after_bytes bytes"

find "$work/rootfs/etc/init.d" -maxdepth 1 -type f -perm /111 -printf '%f\n' |
    LC_ALL=C sort > "$release_dir/startup-services.txt"

python3 "$script_dir/generate_rootfs_manifest.py" "$work/rootfs"     "$work/rootfs/etc/airlink-files.manifest"

bsp="$work/rootfs/mnt/system/ko/3rd/aic8800_bsp.ko"
[ -f "$bsp" ] || die "AIC8800 BSP missing in staged RootFS"
strings "$bsp" | grep -F 'SDIO clock transition done:' >/dev/null ||
    die "staged AIC8800 BSP lacks high-speed transition"
strings "$bsp" | grep -F '/usr/lib/firmware/aic8800_sdio/aic8800' >/dev/null ||
    die "staged AIC8800 firmware path mismatch"
firmware_root="$work/rootfs/usr/lib/firmware/aic8800_sdio"
firmware_compat="$firmware_root/aic8800"
firmware_target="$firmware_root/aic8800_and_aic8800D80"
[ -L "$firmware_compat" ] ||
    die "staged AIC8800 compatibility firmware path is not a symlink"
[ -d "$firmware_target" ] ||
    die "staged AIC8800 firmware target directory missing"
[ "$(readlink -f "$firmware_compat")" = "$(readlink -f "$firmware_target")" ] ||
    die "staged AIC8800 compatibility firmware path resolves incorrectly"
for name in aic_userconfig.txt fmacfw.bin fw_adid_u03.bin fw_patch_u03.bin \
    fw_patch_table_u03.bin; do
    [ -s "$firmware_compat/$name" ] ||
        die "staged AIC8800 firmware missing or empty: $firmware_compat/$name"
done

python3 "$root/fsbl/plat/cv181x/fiptool.py" genfip     --OLD_FIP "$sdk/fip.bin"     --BLCP_2ND "$components/c906l/r26-lvgl.bin"     --BLCP_2ND_RUNADDR 0x8fe00000     "$work/fip/fip.bin"
cp "$sdk/rawimages/boot.sd" "$work/boot.sd"

python3 "$script_dir/extract_fit_property.py" "$work/boot.sd"     /images/fdt-sg2002_licheervnano_sd data "$work/kernel.dtb" >/dev/null
[ "$(fdtget -t s "$work/kernel.dtb" /spi0@04180000 status)" = disabled ] ||
    die "Linux SPI0 must be disabled for C906L ownership"
[ "$(fdtget -t s "$work/kernel.dtb" /i2c@04040000 status)" = disabled ] ||
    die "Linux I2C4 must be disabled for C906L touch ownership"
[ "$(fdtget -t s "$work/kernel.dtb" /saradc status)" = disabled ] ||
    die "Linux SARADC must be disabled for C906L ownership"

rm -f -- "$work/boot.fat" "$work/rootfs.ext4" "$image"
truncate -s 16M "$work/boot.fat"
mkfs.vfat -F 16 -n boot -i A17A2723 "$work/boot.fat" >/dev/null
printf '%s\n' "$image_name" > "$work/ver"
cp "$root/airlink/rootfs/board" "$work/board"
: > "$work/usb.host"
touch -d '2026-08-24T00:00:00Z' "$work/fip/fip.bin" "$work/boot.sd"     "$work/ver" "$work/board" "$work/usb.host"
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$work/boot.fat" "$work/fip/fip.bin" ::/fip.bin
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$work/boot.fat" "$work/boot.sd" ::/boot.sd
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$work/boot.fat" "$work/usb.host" ::/usb.host
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$work/boot.fat" "$work/ver" ::/ver
MTOOLS_SKIP_CHECK=1 mcopy -o -i "$work/boot.fat" "$work/board" ::/board

truncate -s 1600M "$work/rootfs.ext4"
mkfs.ext4 -q -F -L rootfs -U 4149524c-494e-4b52-a727-660023000001     -E root_owner=0:0,lazy_itable_init=0,lazy_journal_init=0     -d "$work/rootfs" "$work/rootfs.ext4"

truncate -s "$IMAGE_SIZE_BYTES" "$image"
sfdisk "$image" >/dev/null <<'SFDISK'
label: dos
label-id: 0x00000000
unit: sectors

start=1, size=32768, type=c, bootable
start=32769, size=3276800, type=83
SFDISK

dd if="$work/boot.fat" of="$image" bs=512 seek=1 conv=notrunc status=none
dd if="$work/rootfs.ext4" of="$image" bs=512 seek=32769 conv=notrunc status=none
sync

(cd "$release_dir" && sha256sum "$image_name" > SHA256SUMS)

python3 "$script_dir/generate_sbom.py" "$root" "$release_dir/SBOM.spdx.json"
{
    printf 'product=%s\n' "$AIRLINK_PRODUCT"
    printf 'version=%s\n' "$AIRLINK_VERSION"
    printf 'build=%s\n' "$AIRLINK_BUILD"
    printf 'tag=%s\n' "$AIRLINK_TAG"
    printf 'profile=release\n'
    printf 'build_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf 'source_commit=%s\n' "$(git -C "$root" rev-parse HEAD)"
    printf 'source_snapshot=%s\n' "$SOURCE_SNAPSHOT_COMMIT"
    printf 'image=%s\n' "$image_name"
    printf 'image_sha256=%s\n' "$(sha256 "$image")"
    printf 'image_size=%s\n' "$(stat -c %s "$image")"
    printf 'c906l_sha256=%s\n' "$(sha256 "$components/c906l/r26-lvgl.bin")"
    printf 'airlinkd_sha256=%s\n' "$(sha256 "$components/linux/airlinkd")"
    printf 'airlinkctl_sha256=%s\n' "$(sha256 "$components/linux/airlinkctl")"
    printf 'aic8800_bsp_sha256=%s\n' "$(sha256 "$bsp")"
    printf 'virtualhere_sha256=%s\n' "$(sha256 "$root/airlink/rootfs/usr/bin/vhusbdriscv64")"
} > "$release_dir/build-info.txt"

printf 'AirLink release image assembled: %s\n' "$image"
