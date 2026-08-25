#!/usr/bin/env python3
from pathlib import Path
import re

root = Path(__file__).resolve().parent
header = (root.parent / "ipc" / "airlink_ipc_v4.h").read_text()
firmware = (root / "ipc_smoke.c").read_text()

checks = {
    "feature": "AIRLINK_IPC_FEATURE_WIFI_PROVISION" in header,
    "request": "AIRLINK_IPC_MSG_WIFI_PROVISION_REQUEST = 0x108" in header,
    "cancel": "AIRLINK_IPC_MSG_WIFI_PROVISION_CANCEL = 0x109" in header,
    "ack": "AIRLINK_IPC_MSG_WIFI_PROVISION_ACK = 9" in header,
    "cancel ack": "AIRLINK_IPC_MSG_WIFI_PROVISION_CANCEL_ACK = 10" in header,
    "offset": "AIRLINK_IPC_PROVISION_STATUS_OFFSET 0x300U" in header,
    "layout": "AIRLINK_IPC_LAYOUT_SIZE          0x380U" in header,
    "status size": 'sizeof(struct airlink_ipc_provision_status) == 128' in header,
    "status fields": re.search(
        r"char ap_ssid\[24\];\s+char ap_password\[16\];\s+"
        r"char target_ssid\[32\];", header) is not None,
    "R27P": "0x50373252U" in firmware,
    "ABI4": "#define ABI_REVISION                    4U" in firmware,
    "hotspot marker": "WIFI provisioning=HOTSPOT+CAPTIVE_PORTAL ipc=ABI4" in firmware,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit("AirLink IPC v1 ABI4 selftest FAIL: " + ", ".join(failed))
print("AirLink IPC v1 ABI4 provisioning header selftest: PASS")
