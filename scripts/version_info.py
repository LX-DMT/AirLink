#!/usr/bin/env python3
"""Read the locked AirLink release metadata shared by build scripts."""

from pathlib import Path
import re

_KEY = re.compile(r"^[A-Z][A-Z0-9_]*$")
_REQUIRED = {
    "AIRLINK_PRODUCT", "AIRLINK_VERSION", "AIRLINK_BUILD", "AIRLINK_TAG",
    "AIRLINK_IMAGE", "C906L_FIRMWARE", "LINUX_FIRMWARE", "IPC_PROTOCOL",
    "IPC_ABI", "SDIO_REQUESTED_HZ", "SDIO_EXPECTED_ACTUAL_HZ",
}


def load_versions(root: Path) -> dict[str, str]:
    path = root / "versions.lock"
    values: dict[str, str] = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ValueError(f"{path}:{number}: expected KEY=VALUE")
        key, value = line.split("=", 1)
        if not _KEY.fullmatch(key) or not value:
            raise ValueError(f"{path}:{number}: invalid locked value")
        values[key] = value
    missing = sorted(_REQUIRED - values.keys())
    if missing:
        raise ValueError(f"{path}: missing locked values: {', '.join(missing)}")
    return values
