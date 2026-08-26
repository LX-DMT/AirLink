#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$script_dir/common.sh"
root="$(repo_root)"
log_dir="$root/out/logs"
mkdir -p "$log_dir"
log="$log_dir/sdk-build.log"

(
    cd "$root"
    # This script is normally launched by the repository top-level Makefile.
    # GNU make exports MAKELEVEL/MAKEFLAGS to recipes; the vendor setup script
    # captures stdout from make print-target-packages-* into compiler flags.
    # Start an independent make tree so directory messages cannot enter CFLAGS.
    unset MAKEFLAGS MFLAGS MAKELEVEL MAKE_TERMOUT MAKE_TERMERR
    # Buildroot rejects WSL interop PATH entries such as /mnt/c/Program Files.
    # Use a deterministic Linux base PATH; cvisetup appends repository toolchains.
    export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
    # The round GC9A01 display is owned by C906L from power-on. U-Boot must not
    # initialize its unrelated MIPI panel/boot-logo path or rewrite display pinmux.
    export ENABLE_BOOTLOGO=0
    # Vendor SDK setup and build functions intentionally use optional unset
    # environment variables, so nounset cannot remain enabled in this subshell.
    set +u
    # shellcheck disable=SC1091
    source build/cvisetup.sh
    defconfig sg2002_licheervnano_sd
    clean_all

    # clean_all does not remove Buildroot's in-tree output or every previous
    # install artifact. Remove them explicitly so a successful release can
    # never be assembled from stale RootFS, Kernel, DTB or FIP files. Keep
    # buildroot/dl only as a normal source-download cache; the clean-WSL
    # validation starts without that directory populated.
    rm -rf -- "$root/buildroot/output" "$OUTPUT_DIR"
    rm -rf -- "$root/buildroot/board/cvitek/SG200X/overlay/mnt/system"
    # The vendor middleware build assumes these output directories already exist.
    # Git cannot preserve the ignored empty directories in a fresh clone.
    mkdir -p "$root/middleware/v2/lib/3rd"

    # Mirror the compile portion of the vendor build_all function, but stop
    # after the source-built RootFS is ready. AirLink assembles its own final
    # MBR/FAT/ext4 image, so vendor OTA CIMG, USB-download and burn-image
    # packaging are both unnecessary and incompatible with the fixed layout.
    build_uboot
    build_kernel
    build_ramboot
    build_osdrv
    build_3rd_party
    build_middleware
    if [ "$TPU_REL" = 1 ]; then
        build_tpu_sdk
    fi
    pack_cfg
    pack_rootfs
) 2>&1 | tee "$log"

sdk="$root/install/soc_sg2002_licheervnano_sd"
sdk_rootfs="$root/buildroot/output/target"
for path in "$sdk/fip.bin" "$sdk/rawimages/boot.sd" "$sdk_rootfs/etc/init.d"; do
    [ -e "$path" ] || die "SDK build output missing: $path"
done
[ -f "$sdk_rootfs/mnt/system/ko/3rd/aic8800_bsp.ko" ] ||
    die "AIC8800 BSP module missing from source-built Buildroot target"
strings "$sdk_rootfs/mnt/system/ko/3rd/aic8800_bsp.ko" |
    grep -F 'SDIO clock transition done:' >/dev/null ||
    die "SDK did not build the integrated 50 MHz AIC8800 BSP"

firmware_root="$sdk_rootfs/usr/lib/firmware/aic8800_sdio"
firmware_compat="$firmware_root/aic8800"
firmware_target="$firmware_root/aic8800_and_aic8800D80"
[ -L "$firmware_compat" ] ||
    die "AIC8800 compatibility firmware path is not a symlink: $firmware_compat"
[ -d "$firmware_target" ] ||
    die "AIC8800 firmware target directory missing: $firmware_target"
[ "$(readlink -f "$firmware_compat")" = "$(readlink -f "$firmware_target")" ] ||
    die "AIC8800 compatibility firmware path resolves to the wrong directory"
for name in aic_userconfig.txt fmacfw.bin fw_adid_u03.bin fw_patch_u03.bin \
    fw_patch_table_u03.bin; do
    [ -s "$firmware_compat/$name" ] ||
        die "AIC8800 required firmware missing or empty: $firmware_compat/$name"
done

printf 'AirLink SG2002 SDK build: PASS\n'
