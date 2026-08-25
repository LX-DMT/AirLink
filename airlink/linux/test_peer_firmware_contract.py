#!/usr/bin/env python3
from pathlib import Path
import re

root = Path(__file__).resolve().parent
daemon = (root / "airlinkd.c").read_text()
firmware = (root.parent / "c906l" / "ipc_smoke.c").read_text()

def define(text, name):
    match = re.search(rf"^#define\s+{name}\s+(0x[0-9A-Fa-f]+)U?", text,
                      re.MULTILINE)
    if not match:
        raise SystemExit(f"missing {name}")
    return int(match.group(1), 16)

linux_expected = define(daemon, "C906L_FW_ID")
c906_actual = define(firmware, "FIRMWARE_ID")
linux_id = define(daemon, "AIRLINKD_FW_ID")
if linux_expected != c906_actual:
    raise SystemExit(
        "R27.6.6.22 peer firmware contract FAIL: "
        f"airlinkd expects 0x{linux_expected:08x}, "
        f"C906L publishes 0x{c906_actual:08x}"
    )
if linux_expected != 0x50373252:
    raise SystemExit("R27.6.6.22 peer firmware contract FAIL: peer is not R27P")
if linux_id != 0x37324E4C:
    raise SystemExit("R27.6.6.22 peer firmware contract FAIL: Linux is not LN27")
print(
    "R27.6.6.22 peer firmware contract: PASS "
    f"(Linux LN27 -> C906L R27P 0x{c906_actual:08x})"
)
