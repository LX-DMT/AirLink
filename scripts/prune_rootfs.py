#!/usr/bin/env python3
import argparse
import glob
import os
from pathlib import Path
import shutil

COMMON_PATHS = [
    "usr/lib/python3.11",
    "usr/lib/qt",
    "usr/lib/metatypes",
    "usr/lib/tcl8",
    "usr/lib/tcl8.6",
    "usr/lib/tcllib1.21",
    "usr/lib/tdbc1.1.5",
    "usr/lib/itcl4.2.3",
    "usr/lib/expect5.45.4",
    "usr/lib/avahi",
    "usr/lib/named",
    "usr/bin/dl_lib",
    "usr/bin/lib",
    "usr/include",
    "usr/share/fonts",
    "usr/share/cursors",
    "usr/share/icons",
    "usr/share/vim",
    "usr/share/gdb",
    "usr/share/font-awesome",
    "usr/share/opencv4",
    "usr/share/ffmpeg",
    "usr/share/applications",
    "usr/share/pixmaps",
    "usr/share/bash-completion",
    "usr/share/doc",
    "usr/share/man",
    "usr/share/info",
    "usr/share/aclocal",
    "usr/share/misc",
    "usr/share/avahi",
    "usr/share/dbus-1",
    "mnt/system/usr/bin",
    "mnt/system/usr/lib",
    "etc/avahi",
    "var/lib/avahi",
]

COMMON_GLOBS = [
    "usr/bin/python*",
    "usr/bin/pip*",
    "usr/bin/2to3*",
    "usr/bin/pydoc*",
    "usr/bin/idle*",
    "usr/bin/sip",
    "usr/bin/AirLinkUiApp",
    "usr/bin/ffmpeg",
    "usr/bin/ffprobe",
    "usr/bin/gdb",
    "usr/bin/gdbserver",
    "usr/bin/vim*",
    "usr/bin/stress-ng",
    "usr/bin/nn_*",
    "usr/bin/model_runner",
    "usr/bin/*.cvimodel",
    "usr/bin/fbv",
    "usr/bin/fbbar",
    "usr/bin/fbpattern",
    "usr/bin/ts_calibrate",
    "usr/bin/input-event-daemon",
    "usr/bin/evtest",
    "usr/bin/aplay",
    "usr/bin/arecord",
    "usr/bin/amixer",
    "usr/bin/alsamixer",
    "usr/bin/mpg123",
    "usr/bin/out123",
    "usr/bin/dbus-*",
    "usr/bin/gdbus",
    "usr/bin/avahi-*",
    "usr/bin/ntpdate",
    "usr/bin/adbd",
    "usr/bin/aircrack-ng",
    "usr/bin/airdecap-ng",
    "usr/bin/airdecloak-ng",
    "usr/bin/airolib-ng",
    "usr/bin/packetforge-ng",
    "usr/bin/gnuchess",
    "usr/bin/ipmitool",
    "usr/bin/sqlite3",
    "usr/bin/curl",
    "usr/bin/hb-*",
    "usr/bin/icuexportdata",
    "usr/bin/pkgdata",
    "usr/bin/xmllint",
    "usr/sbin/avahi-*",
    "usr/sbin/ssdpd",
    "usr/sbin/ntpd",
    "usr/sbin/iptables*",
    "usr/sbin/ip6tables*",
    "usr/sbin/nft",
    "usr/lib/libpython*.so*",
    "usr/lib/libQt5*.so*",
    "usr/lib/libopencv*.so*",
    "usr/lib/libavcodec*.so*",
    "usr/lib/libavdevice*.so*",
    "usr/lib/libavfilter*.so*",
    "usr/lib/libavformat*.so*",
    "usr/lib/libavutil*.so*",
    "usr/lib/libpostproc*.so*",
    "usr/lib/libswresample*.so*",
    "usr/lib/libswscale*.so*",
    "usr/lib/libicu*.so*",
    "usr/lib/libprotobuf*.so*",
    "usr/lib/libcvi*.so*",
    "usr/lib/libcvikernel*.so*",
    "usr/lib/libasound*.so*",
    "usr/lib/libmpg123*.so*",
    "usr/lib/libharfbuzz*.so*",
    "usr/lib/libfontconfig*.so*",
    "usr/lib/libfreetype*.so*",
    "usr/lib/libtcl*.so*",
    "usr/lib/libtk*.so*",
    "usr/lib/libavahi*.so*",
    "usr/lib/libglib*.so*",
    "usr/lib/libgio*.so*",
    "usr/lib/libgobject*.so*",
    "usr/lib/libxml*.so*",
    "usr/lib/libxslt*.so*",
    "usr/lib/libtiff*.so*",
    "usr/lib/libjpeg*.so*",
    "usr/lib/libpng*.so*",
    "usr/lib/libwebp*.so*",
    "usr/lib/libsqlite*.so*",
    "usr/lib/libzmq*.so*",
    "usr/lib/libwebsockets*.so*",
    "usr/lib/libcurl*.so*",
    "usr/lib/libarchive*.so*",
    "usr/lib/libnft*.so*",
    "usr/lib/libxtables*.so*",
    "usr/lib/*.a",
    "usr/lib/*.la",
    "usr/lib/pkgconfig",
    "usr/lib/cmake",
]

COMMON_INIT = [
    "S30dbus",
    "S35iptables",
    "S40network",
    "S49ntp",
    "S50avahi-daemon",
    "S50ssdpd",
    "S99input-event-daemon",
    "S99resizefs",
]

RELEASE_PATHS = [
    "etc/ssh",
    "root/.ssh",
    "usr/libexec/sftp-server",
]

RELEASE_INIT_KEEP = {
    "S01fs",
    "S01seedrng",
    "S01syslogd",
    "S02klogd",
    "S02sysctl",
    "S07fs2",
    "S08usbdev",
    "S10udev",
    "S21haveged",
    "S25wifimod",
    "S29airlinkd",
    "rcK",
    "rcS",
}

RELEASE_WIFI_MODULE_KEEP = {
    "cfg80211.ko",
    "3rd/aic8800_bsp.ko",
    "3rd/aic8800_fdrv.ko",
}

RELEASE_GLOBS = [
    "usr/bin/ssh",
    "usr/bin/ssh-*",
    "usr/bin/scp",
    "usr/bin/sftp",
    "usr/bin/mosh-*",
    "usr/bin/tcpdump",
    "usr/bin/strace",
    "usr/bin/iperf3",
    "usr/bin/telnet*",
    "usr/sbin/sshd",
    "usr/sbin/telnetd",
    "usr/sbin/dropbear*",
]

def tree_size(path: Path) -> int:
    try:
        if path.is_symlink() or path.is_file():
            return path.lstat().st_size
        total = path.lstat().st_size
        for item in path.rglob("*"):
            try:
                total += item.lstat().st_size
            except FileNotFoundError:
                pass
        return total
    except FileNotFoundError:
        return 0

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("root")
    parser.add_argument("profile", choices=("debug", "release"))
    parser.add_argument("report")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    if root == Path("/") or not (root / "etc/init.d").is_dir():
        raise SystemExit("unsafe or invalid RootFS path")
    if not (root / "usr/sbin/airlinkd").exists():
        raise SystemExit("airlinkd missing before pruning")

    removed = []
    seen = set()

    def remove(path: Path, reason: str) -> None:
        try:
            rel = path.relative_to(root).as_posix()
        except ValueError:
            raise SystemExit(f"refusing path outside RootFS: {path}")
        if rel in ("", ".") or rel in seen or not path.exists() and not path.is_symlink():
            return
        seen.add(rel)
        size = tree_size(path)
        if path.is_dir() and not path.is_symlink():
            shutil.rmtree(path)
        else:
            path.unlink(missing_ok=True)
        removed.append((rel, size, reason))

    for rel in COMMON_PATHS:
        remove(root / rel, "unused-runtime-family")
    for pattern in COMMON_GLOBS:
        for match in glob.glob(str(root / pattern)):
            remove(Path(match), "unused-runtime-family")

    init_dir = root / "etc/init.d"
    for name in COMMON_INIT:
        remove(init_dir / name, "disabled-startup-service")
    for path in init_dir.glob("disabled.*"):
        remove(path, "legacy-disabled-service")

    if args.profile == "release":
        for rel in RELEASE_PATHS:
            remove(root / rel, "release-network-hardening")
        for pattern in RELEASE_GLOBS:
            for match in glob.glob(str(root / pattern)):
                remove(Path(match), "release-network-hardening")
        remove(init_dir / "S50sshd", "release-network-hardening")

        # The vendor overlay contains camera/TPU/audio demos, legacy network
        # setup, Linux display/touch services and USB gadget helpers. Keep a
        # strict startup whitelist so removed programs cannot still run.
        for path in init_dir.iterdir():
            if path.name not in RELEASE_INIT_KEEP:
                remove(path, "release-startup-whitelist")

        # AirLink only needs the AIC8800 WLAN stack. USB host support is built
        # into the Kernel, while video/ISP/TPU/RTC/SARADC and alternate WLAN
        # modules are intentionally not loaded by the Release image.
        module_root = root / "mnt/system/ko"
        if module_root.exists():
            for path in sorted(module_root.rglob("*"), key=lambda item: len(item.parts), reverse=True):
                if path.is_file() or path.is_symlink():
                    rel = path.relative_to(module_root).as_posix()
                    if rel not in RELEASE_WIFI_MODULE_KEEP:
                        remove(path, "release-kernel-module-whitelist")
            for path in sorted(module_root.rglob("*"), key=lambda item: len(item.parts), reverse=True):
                if path.is_dir() and not path.is_symlink():
                    try:
                        path.rmdir()
                    except OSError:
                        pass

    # Remove empty development directories, but never touch runtime mountpoints.
    for base in (root / "usr/share", root / "usr/lib"):
        if base.exists():
            for path in sorted(base.rglob("*"), key=lambda p: len(p.parts), reverse=True):
                if path.is_dir() and not path.is_symlink():
                    try:
                        path.rmdir()
                    except OSError:
                        pass

    removed.sort()
    report = Path(args.report)
    report.parent.mkdir(parents=True, exist_ok=True)
    total = sum(size for _, size, _ in removed)
    with report.open("w", encoding="utf-8") as handle:
        handle.write(f"AirLink R27 deleted paths profile={args.profile}\n")
        handle.write(f"entries={len(removed)} logical_bytes={total}\n\n")
        for rel, size, reason in removed:
            handle.write(f"{size:12d} {reason:28s} /{rel}\n")

if __name__ == "__main__":
    main()
