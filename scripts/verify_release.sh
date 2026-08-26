#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$script_dir/common.sh"
root="$(repo_root)"
load_versions
release="$root/out/release"
image="$release/$AIRLINK_IMAGE"
work="$root/out/verify-release"

[ -f "$image" ] || die "release image missing; run make release"
[ "$(stat -c %s "$image")" = "$IMAGE_SIZE_BYTES" ] ||
    die "release image size mismatch"

rm -rf -- "$work"
mkdir -p "$work/boot" "$work/rootfs"

sfdisk --json "$image" > "$work/partitions.json"
python3 - "$work/partitions.json" <<'PY'
import json, sys
parts = json.load(open(sys.argv[1]))["partitiontable"]["partitions"]
expected = [(1, 32768, "c"), (32769, 3276800, "83")]
actual = [(p["start"], p["size"], str(p["type"]).lower().replace("0x", "")) for p in parts]
if actual != expected:
    raise SystemExit(f"partition mismatch: {actual}")
PY

for name in fip.bin boot.sd usb.host ver board; do
    MTOOLS_SKIP_CHECK=1 mcopy -o -i "$image"@@512 "::/$name" "$work/boot/$name"
done
[ ! -e "$work/boot/wifi.sta" ] || die "test Wi-Fi credentials remain"

python3 "$script_dir/inspect_fip.py" "$work/boot/fip.bin"     --json-out "$release/fip-components.json"     --text-out "$release/fip-components.txt"     --source-label "$image::/fip.bin" >/dev/null
python3 - "$release/fip-components.json" "$root/out/components/c906l/r26-lvgl.bin" \
    "$work/boot/fip.bin" <<'PY'
import json
import lzma
import sys

report = json.load(open(sys.argv[1], encoding="utf-8"))
firmware = open(sys.argv[2], "rb").read()
fip = open(sys.argv[3], "rb").read()
items = {item["name"]: item for item in report["components"]}
actual = items.get("BLCP_2ND")
if not actual or not actual.get("present"):
    raise SystemExit("BLCP_2ND is missing from release FIP")
offset = actual.get("offset")
size = actual.get("size")
if not isinstance(offset, int) or not isinstance(size, int):
    raise SystemExit("BLCP_2ND offset/size is invalid")
if size < len(firmware):
    raise SystemExit(
        f"BLCP_2ND is truncated: component={size}, firmware={len(firmware)}"
    )
payload = fip[offset:offset + len(firmware)]
padding = fip[offset + len(firmware):offset + size]
if payload != firmware:
    raise SystemExit("BLCP_2ND payload differs from the current C906L firmware")
if any(padding):
    raise SystemExit("BLCP_2ND alignment padding is not zero-filled")
if actual.get("run_address") != 0x8FE00000:
    raise SystemExit(
        f"BLCP_2ND run address mismatch: {actual.get('run_address')!r}"
    )
loader = items.get("LOADER_2ND")
if not loader or not loader.get("present"):
    raise SystemExit("LOADER_2ND is missing from release FIP")
loader_offset = loader.get("offset")
loader_size = loader.get("size")
if not isinstance(loader_offset, int) or not isinstance(loader_size, int):
    raise SystemExit("LOADER_2ND offset/size is invalid")
loader_image = fip[loader_offset:loader_offset + loader_size]
if loader_image[4:8] != b"B3MA":
    raise SystemExit("LOADER_2ND is not the expected LZMA image")
try:
    decoder = lzma.LZMADecompressor(format=lzma.FORMAT_ALONE)
    uboot = decoder.decompress(loader_image[32:])
except lzma.LZMAError as exc:
    raise SystemExit(f"LOADER_2ND decompression failed: {exc}") from exc
for marker in (b"cvi_vo_probe", b"zct2133v1", b"showlogo="):
    if marker in uboot:
        raise SystemExit(
            f"U-Boot still contains display-owner conflict marker: {marker!r}"
        )
PY
python3 "$script_dir/extract_fit_property.py" "$work/boot/boot.sd"     /images/kernel-1 data "$work/boot/kernel.bin" >/dev/null
python3 "$script_dir/extract_fit_property.py" "$work/boot/boot.sd"     /images/ramdisk-1 data "$work/boot/ramdisk.bin" >/dev/null
python3 "$script_dir/extract_fit_property.py" "$work/boot/boot.sd"     /images/fdt-sg2002_licheervnano_sd data "$work/boot/kernel.dtb" >/dev/null

[ "$(fdtget -t s "$work/boot/kernel.dtb" /spi0@04180000 status)" = disabled ]
[ "$(fdtget -t s "$work/boot/kernel.dtb" /i2c@04040000 status)" = disabled ]
[ "$(fdtget -t s "$work/boot/kernel.dtb" /saradc status)" = disabled ]
[ "$(fdtget -t s "$work/boot/kernel.dtb" /mipi_tx status)" = disabled ]
[ "$(fdtget -t s "$work/boot/kernel.dtb" /vo status)" = disabled ]
for node in \
    /pwm@3060000 /pwm@3061000 /pwm@3062000 \
    /serial@04150000 /serial@04160000 /serial@04170000 \
    /i2c@04000000 /i2c@04010000 /i2c@04020000 /i2c@04030000 \
    /dac@0300A000 /cif
do
    [ "$(fdtget -t s "$work/boot/kernel.dtb" "$node" status)" = disabled ] ||
        die "Linux ownership node is not disabled: $node"
done
if fdtget "$work/boot/kernel.dtb" /spi4@gpio compatible >/dev/null 2>&1; then
    die "legacy spi4-gpio node remains"
fi
if fdtget "$work/boot/kernel.dtb" /i2c5@gpio compatible >/dev/null 2>&1; then
    die "legacy i2c5-gpio node remains"
fi

dd if="$image" of="$work/rootfs/rootfs.ext4" bs=4M skip=16777728     iflag=skip_bytes,count_bytes count=1677721600 status=none
e2fsck -fn "$work/rootfs/rootfs.ext4" > "$release/e2fsck.txt" 2>&1 || {
    rc=$?
    [ "$rc" -le 1 ] || { cat "$release/e2fsck.txt" >&2; exit "$rc"; }
}

dump()
{
    debugfs -R "dump -p $1 $2" "$work/rootfs/rootfs.ext4" >/dev/null 2>&1
}
missing()
{
    debugfs -R "stat $1" "$work/rootfs/rootfs.ext4" 2>&1 | grep -q 'File not found'
}

dump /usr/sbin/airlinkd "$work/rootfs/airlinkd"
dump /usr/sbin/airlinkctl "$work/rootfs/airlinkctl"
dump /usr/bin/vhusbdriscv64 "$work/rootfs/vhusbdriscv64"
dump /mnt/system/ko/3rd/aic8800_bsp.ko "$work/rootfs/aic8800_bsp.ko"
dump /mnt/system/ko/3rd/aic8800_fdrv.ko "$work/rootfs/aic8800_fdrv.ko"
[ -s "$work/rootfs/aic8800_fdrv.ko" ] || die "AIC8800 fdrv image readback missing"
dump /etc/airlink-release "$work/rootfs/airlink-release"

firmware_base=/usr/lib/firmware/aic8800_sdio
firmware_link_stat="$(debugfs -R "stat $firmware_base/aic8800" \
    "$work/rootfs/rootfs.ext4" 2>&1)"
printf '%s\n' "$firmware_link_stat" | grep -F 'aic8800_and_aic8800D80' >/dev/null ||
    die "AIC8800 compatibility firmware symlink missing from final image"
mkdir -p "$work/rootfs/aic8800-firmware"
for name in aic_userconfig.txt fmacfw.bin fw_adid_u03.bin fw_patch_u03.bin \
    fw_patch_table_u03.bin; do
    output="$work/rootfs/aic8800-firmware/$name"
    rm -f -- "$output"
    dump "$firmware_base/aic8800/$name" "$output"
    [ -s "$output" ] ||
        die "AIC8800 firmware is not readable through compatibility path: $name"
done

cmp -s "$work/rootfs/airlinkd" "$root/out/components/linux/airlinkd" ||
    die "airlinkd image readback mismatch"
cmp -s "$work/rootfs/airlinkctl" "$root/out/components/linux/airlinkctl" ||
    die "airlinkctl image readback mismatch"
[ "$(sha256 "$work/rootfs/vhusbdriscv64")" = "$VIRTUALHERE_RISCV64_SHA256" ] ||
    die "VirtualHere image readback mismatch"
strings "$work/rootfs/aic8800_bsp.ko" | grep -F 'SDIO clock transition done:' >/dev/null ||
    die "AIC8800 50 MHz transition marker missing"
if strings "$work/rootfs/aic8800_fdrv.ko" | grep -F 'btsdio_init' >/dev/null; then
    die "AIC8800 fdrv unexpectedly includes SDIO Bluetooth"
fi

for banned in /usr/sbin/sshd /usr/bin/ssh /usr/bin/telnet /usr/sbin/telnetd     /usr/sbin/dropbear /usr/sbin/dropbearmulti /usr/bin/adbd /bin/adbd /sbin/adbd     /etc/init.d/S50sshd /etc/ssh/sshd_config /etc/dropbear; do
    missing "$banned" || die "release-banned path remains: $banned"
done
grep -F "version=$AIRLINK_VERSION" "$work/rootfs/airlink-release" >/dev/null
grep -F "build=$AIRLINK_BUILD" "$work/rootfs/airlink-release" >/dev/null
grep -F 'profile=release' "$work/rootfs/airlink-release" >/dev/null

(cd "$release" && sha256sum "$AIRLINK_IMAGE" > SHA256SUMS)
python3 "$script_dir/generate_release_manifest.py" "$release" "$release/manifest.json"

printf 'AirLink release verification: PASS\n'
printf '  image: %s\n  SHA256: %s\n' "$image" "$(sha256 "$image")"
