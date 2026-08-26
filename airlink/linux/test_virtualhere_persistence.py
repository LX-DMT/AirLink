#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parent
daemon = (root / "airlinkd.c").read_text(encoding="utf-8")
init = (root / "S29airlinkd").read_text(encoding="utf-8")

assert '#define VH_CONFIG_DIR "/data/airlink"' in daemon
assert '#define VH_CONFIG_PATH "/data/airlink/vhusbd.ini"' in daemon
assert '#define VH_CONFIG_TEMP "/data/airlink/vhusbd.ini.tmp"' in daemon
writer = daemon[daemon.index("static bool write_virtualhere_config"):
                daemon.index("static void log_virtualhere_tail")]
assert "stat(VH_CONFIG_PATH, &status) == 0" in writer
assert "S_ISREG(status.st_mode)" in writer
assert "status.st_size == 0" in writer
assert "preserve=YES" in writer
assert writer.index("return true;") < writer.index('fopen(VH_CONFIG_TEMP, "w")')
assert "fchmod(fd, 0600)" in writer
assert "fsync(fd)" in writer
assert "rename(VH_CONFIG_TEMP, VH_CONFIG_PATH)" in writer
assert "fsync(dirfd)" in writer
assert "/data/airlink/vhusbd.ini" not in init
assert "/run/airlink/vhusbd.ini" not in daemon
print("VirtualHere persistent activation/config tests: PASS")
