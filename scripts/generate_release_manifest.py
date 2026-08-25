#!/usr/bin/env python3
from pathlib import Path
import hashlib
import json
import sys

from version_info import load_versions

if len(sys.argv) != 3:
    raise SystemExit("usage: generate_release_manifest.py RELEASE_DIR OUTPUT")
release = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
repo = release.parents[1]
versions = load_versions(repo)

items = []
for path in sorted(release.iterdir()):
    if not path.is_file() or path == output:
        continue
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    items.append({
        "name": path.name,
        "size": path.stat().st_size,
        "sha256": digest.hexdigest(),
    })

doc = {
    "product": versions["AIRLINK_PRODUCT"],
    "version": versions["AIRLINK_VERSION"],
    "build": versions["AIRLINK_BUILD"],
    "tag": versions["AIRLINK_TAG"],
    "image": versions["AIRLINK_IMAGE"],
    "profile": "release",
    "c906l": versions["C906L_FIRMWARE"],
    "linux": versions["LINUX_FIRMWARE"],
    "ipc_protocol": int(versions["IPC_PROTOCOL"]),
    "ipc_abi": int(versions["IPC_ABI"]),
    "sdio_requested_hz": int(versions["SDIO_REQUESTED_HZ"]),
    "sdio_expected_actual_hz": int(versions["SDIO_EXPECTED_ACTUAL_HZ"]),
    "artifacts": items,
}
output.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
