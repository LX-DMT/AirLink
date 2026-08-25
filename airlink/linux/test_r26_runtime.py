#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
from pathlib import Path

binary = Path(sys.argv[1])
status = subprocess.check_output([str(binary), "--status-selftest"],
                                 text=True).strip()
parsed = json.loads(status)
assert parsed["version"] == "R27.6.6.22"
assert parsed["mode"] == "wired"
assert parsed["phase"] == "WIRED_READY"
assert parsed["ch347"] == 3
assert parsed["ipc"]["abi"] == 4
assert parsed["ipc"]["linux_hb"] == 11
assert parsed["ipc"]["c906l_hb"] == 22
assert parsed["provision"]["phase"] == "IDLE"
assert parsed["wifi"]["bssid"] == ""
assert parsed["wifi"]["default_route"] is False
assert parsed["wifi"]["power_save"] == "unknown"
assert set(parsed["sdio"]) == {"requested_hz", "actual_hz", "timing"}
assert parsed["virtualhere"]["state"] == 0
assert parsed["virtualhere"]["listener"] is False
assert parsed["virtualhere"]["client_connected"] is False
assert parsed["network_hint"] == "none"

cases = {
    "secure.conf": ("""ctrl_interface=/run/wpa_supplicant
network={
    ssid="test-network"
    psk="not-a-real-password"
    key_mgmt=WPA-PSK
}
""", True),
    "unicode.conf": ("""network={
    ssid="AirLink-\u4e2d\u6587"
    psk="placeholder-passphrase"
}
""", True),
    "open-explicit.conf": ("""network={
    ssid="factory-test"
    key_mgmt=NONE
}
""", True),
    "wildcard-open.conf": ("""network={
    key_mgmt=NONE
}
""", False),
    "empty-ssid.conf": ("""network={
    ssid=""
    key_mgmt=NONE
}
""", False),
}
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    for name, (content, expected) in cases.items():
        path = root / name
        path.write_text(content)
        rc = subprocess.run(
            [str(binary), "--wpa-config-check", str(path)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode
        assert (rc == 0) == expected, (name, rc, expected)

print("R27.6.6.22 JSON, fixed-AP selftest and Wi-Fi config runtime tests: PASS")
