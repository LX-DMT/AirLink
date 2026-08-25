#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import re
import subprocess

COMMON_REQUIRED = [
    "bin/busybox",
    "bin/sh",
    "sbin/udevd",
    "usr/bin/udevadm",
    "usr/bin/lsusb",
    "usr/sbin/ip",
    "usr/sbin/iw",
    "usr/sbin/wpa_supplicant",
    "usr/sbin/hostapd",
    "usr/sbin/dnsmasq",
    "usr/bin/vhusbdriscv64",
    "usr/sbin/airlinkd",
    "usr/sbin/airlinkctl",
    "etc/init.d/S08usbdev",
    "etc/init.d/S10udev",
    "etc/init.d/S21haveged",
    "etc/init.d/S25wifimod",
    "etc/init.d/S29airlinkd",
]
DEBUG_REQUIRED = [
    "usr/sbin/sshd",
    "usr/bin/iperf3",
    "usr/bin/tcpdump",
    "usr/bin/strace",
    "etc/init.d/S50sshd",
]
BANNED_INIT = [
    "S30dbus",
    "S35iptables",
    "S40network",
    "S49ntp",
    "S50avahi-daemon",
    "S50ssdpd",
    "S99input-event-daemon",
    "S99resizefs",
]
RELEASE_BANNED = [
    "usr/sbin/sshd",
    "usr/bin/ssh",
    "usr/bin/scp",
    "usr/bin/sftp",
    "usr/bin/tcpdump",
    "usr/bin/strace",
    "usr/bin/iperf3",
    "usr/bin/adbd",
    "bin/adbd",
    "sbin/adbd",
    "usr/bin/telnet",
    "usr/sbin/telnetd",
    "usr/sbin/dropbear",
    "usr/sbin/dropbearmulti",
    "etc/dropbear",
    "etc/ssh",
    "etc/init.d/S50sshd",
]
FAMILY_BANNED = [
    "usr/lib/python3.11",
    "usr/lib/qt",
    "usr/share/fonts",
    "usr/share/cursors",
    "usr/bin/AirLinkUiApp",
    "usr/bin/gdb",
    "usr/bin/vim",
    "usr/bin/stress-ng",
    "mnt/system/usr/bin",
    "mnt/system/usr/lib",
]

NEEDED_RE = re.compile(r"Shared library: \[(.*?)\]")
INTERP_RE = re.compile(r"Requesting program interpreter: (.*?)]")

def elf(path: Path) -> bool:
    try:
        with path.open("rb") as handle:
            return handle.read(4) == b"\x7fELF"
    except OSError:
        return False

def readelf(path: Path):
    dynamic = subprocess.run(
        ["readelf", "-d", str(path)], text=True,
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=False
    ).stdout
    program = subprocess.run(
        ["readelf", "-l", str(path)], text=True,
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, check=False
    ).stdout
    return NEEDED_RE.findall(dynamic), INTERP_RE.findall(program)

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("root")
    parser.add_argument("profile", choices=("debug", "release"))
    parser.add_argument("report")
    args = parser.parse_args()
    root = Path(args.root).resolve()
    if root == Path("/") or not (root / "etc/init.d").is_dir():
        raise SystemExit("unsafe or invalid RootFS path")

    errors = []
    required = COMMON_REQUIRED + (DEBUG_REQUIRED if args.profile == "debug" else [])
    for rel in required:
        path = root / rel
        if not path.exists():
            errors.append(f"missing required path: /{rel}")

    for name in BANNED_INIT:
        if (root / "etc/init.d" / name).exists():
            errors.append(f"banned startup service remains: {name}")
    if any((root / "etc/init.d").glob("disabled.*")):
        errors.append("legacy disabled.* init scripts remain")
    for rel in FAMILY_BANNED:
        if (root / rel).exists():
            errors.append(f"unused family remains: /{rel}")
    if args.profile == "release":
        for rel in RELEASE_BANNED:
            if (root / rel).exists():
                errors.append(f"release-banned path remains: /{rel}")

    library_dirs = [root / "lib", root / "usr/lib"]
    libraries = {}
    for directory in library_dirs:
        if not directory.exists():
            continue
        for path in directory.rglob("*"):
            if path.is_file() or path.is_symlink():
                try:
                    if path.exists():
                        libraries.setdefault(path.name, path)
                except OSError:
                    pass

    # Validate every retained userspace executable, not just the required
    # AirLink entry points. This makes the ELF dependency check a true closure
    # check for the complete pruned RootFS.
    roots = []
    program_dirs = (
        root / "bin",
        root / "sbin",
        root / "usr/bin",
        root / "usr/sbin",
        root / "usr/libexec",
    )
    for directory in program_dirs:
        if not directory.exists():
            continue
        for path in directory.rglob("*"):
            # Buildroot may create absolute applet aliases (for example
            # /sbin/udevadm -> /usr/bin/udevadm). The real executable is also
            # scanned in its canonical directory, so do not resolve aliases
            # against the host filesystem here.
            if path.is_symlink() or not path.is_file():
                continue
            if elf(path):
                roots.append(path)

    queue = list(dict.fromkeys(roots))
    visited = set()
    dependency_lines = []
    while queue:
        path = queue.pop(0)
        key = str(path)
        if key in visited:
            continue
        visited.add(key)
        needed, interpreters = readelf(path)
        shown = "/" + path.relative_to(root).as_posix()
        for interpreter in interpreters:
            target = root / interpreter.lstrip("/")
            if not target.exists():
                errors.append(f"{shown}: missing interpreter {interpreter}")
            else:
                dependency_lines.append(f"{shown} -> {interpreter}")
        for soname in needed:
            target = libraries.get(soname)
            if target is None or not target.exists():
                errors.append(f"{shown}: missing NEEDED {soname}")
                continue
            resolved = target.resolve()
            dependency_lines.append(
                f"{shown} -> {soname} => /{target.relative_to(root).as_posix()}"
            )
            if elf(resolved):
                queue.append(resolved)

    active = []
    for path in sorted((root / "etc/init.d").iterdir()):
        if path.is_file() and os.access(path, os.X_OK):
            active.append(path.name)

    report = Path(args.report)
    report.parent.mkdir(parents=True, exist_ok=True)
    with report.open("w", encoding="utf-8") as handle:
        handle.write(f"AirLink R27 RootFS verification profile={args.profile}\n")
        handle.write("required-runtime dependency closure:\n")
        for line in sorted(set(dependency_lines)):
            handle.write(line + "\n")
        handle.write("\nactive-init:\n")
        for name in active:
            handle.write(name + "\n")
        if errors:
            handle.write("\nERRORS:\n")
            for error in errors:
                handle.write(error + "\n")
        else:
            handle.write("\nPASS\n")

    if errors:
        raise SystemExit("\n".join(errors))

if __name__ == "__main__":
    main()
