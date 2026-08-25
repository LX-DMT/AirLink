#!/usr/bin/env python3
"""Generate the release SPDX 2.3 software bill of materials.

The SDK is intentionally shipped as one source snapshot rather than Git
submodules. This document lists the principal source/runtime packages and the
proprietary payloads that require extra review. It does not claim that every
vendor file was individually license-scanned.
"""

from __future__ import annotations

from datetime import datetime, timezone
from pathlib import Path
import hashlib
import json
import subprocess
import sys

from version_info import load_versions


if len(sys.argv) != 3:
    raise SystemExit("usage: generate_sbom.py ROOT OUTPUT")

root = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2]).resolve()
versions = load_versions(root)
public_version = versions["AIRLINK_VERSION"]
internal_build = versions["AIRLINK_BUILD"]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def git_head() -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(root), "rev-parse", "HEAD"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "source-snapshot"


def package(
    name: str,
    spdx_id: str,
    version: str,
    download: str,
    license_id: str,
    *,
    supplier: str = "NOASSERTION",
    checksum: str | None = None,
    comment: str | None = None,
) -> dict[str, object]:
    item: dict[str, object] = {
        "name": name,
        "SPDXID": spdx_id,
        "versionInfo": version,
        "downloadLocation": download,
        "supplier": supplier,
        "licenseConcluded": license_id,
        "licenseDeclared": license_id,
        "copyrightText": "NOASSERTION",
        "filesAnalyzed": False,
    }
    if checksum:
        item["checksums"] = [{"algorithm": "SHA256", "checksumValue": checksum}]
    if comment:
        item["comment"] = comment
    return item


virtualhere = root / "airlink/rootfs/usr/bin/vhusbdriscv64"
if not virtualhere.is_file():
    raise SystemExit(f"required proprietary payload is missing: {virtualhere}")

commit = git_head()
namespace_seed = hashlib.sha256(
    f"AirLink-{public_version}:{internal_build}:{commit}".encode("utf-8")
).hexdigest()

packages = [
    package(
        "AirLink firmware and services",
        "SPDXRef-Package-AirLink",
        public_version,
        "NOASSERTION",
        "Apache-2.0",
        supplier="Organization: AirLink project",
        comment=f"Project-owned C906L UI, Linux service, IPC ABI4 and portal sources; internal build {internal_build}.",
    ),
    package(
        "SG2002 vendor SDK source snapshot",
        "SPDXRef-Package-SG2002-SDK",
        commit[:12],
        "NOASSERTION",
        "NOASSERTION",
        supplier="Organization: SOPHGO/CVITEK and contributors",
        comment="Aggregate vendor SDK; individual files retain their original license notices.",
    ),
    package(
        "Linux kernel",
        "SPDXRef-Package-Linux",
        "5.10.4",
        "https://www.kernel.org/",
        "GPL-2.0-only",
    ),
    package(
        "U-Boot",
        "SPDXRef-Package-UBoot",
        "2021.10",
        "https://source.denx.de/u-boot/u-boot",
        "GPL-2.0-or-later",
    ),
    package(
        "Buildroot",
        "SPDXRef-Package-Buildroot",
        "2023.11",
        "https://buildroot.org/",
        "GPL-2.0-or-later",
    ),
    package(
        "OpenSBI",
        "SPDXRef-Package-OpenSBI",
        "NOASSERTION",
        "https://github.com/riscv-software-src/opensbi",
        "BSD-2-Clause",
    ),
    package(
        "FreeRTOS kernel",
        "SPDXRef-Package-FreeRTOS",
        "NOASSERTION",
        "https://www.freertos.org/",
        "MIT",
    ),
    package(
        "musl C library",
        "SPDXRef-Package-musl",
        "NOASSERTION",
        "https://musl.libc.org/",
        "MIT",
    ),
    package(
        "BusyBox",
        "SPDXRef-Package-BusyBox",
        "1.36.1",
        "https://busybox.net/",
        "GPL-2.0-only",
    ),
    package(
        "eudev",
        "SPDXRef-Package-eudev",
        "3.2.14",
        "https://github.com/eudev-project/eudev",
        "GPL-2.0-or-later",
    ),
    package(
        "wpa_supplicant",
        "SPDXRef-Package-wpa-supplicant",
        "2.10",
        "https://w1.fi/wpa_supplicant/",
        "BSD-3-Clause",
    ),
    package(
        "hostapd",
        "SPDXRef-Package-hostapd",
        "2.10",
        "https://w1.fi/hostapd/",
        "BSD-3-Clause",
    ),
    package(
        "dnsmasq",
        "SPDXRef-Package-dnsmasq",
        "2.89",
        "https://thekelleys.org.uk/dnsmasq/doc.html",
        "GPL-2.0-or-later",
    ),
    package(
        "AIC8800 Linux driver",
        "SPDXRef-Package-AIC8800-Driver",
        "SG2002-integrated",
        "NOASSERTION",
        "GPL-2.0-only",
        supplier="Organization: AICSemi and vendor SDK contributors",
    ),
    package(
        "AIC8800 firmware blobs",
        "SPDXRef-Package-AIC8800-Firmware",
        "SG2002-integrated",
        "NOASSERTION",
        "LicenseRef-AIC8800-Firmware",
        supplier="Organization: AICSemi",
        comment="Redistribution terms must be reviewed against the vendor delivery terms.",
    ),
    package(
        "LVGL",
        "SPDXRef-Package-LVGL",
        "8.3.11",
        "https://github.com/lvgl/lvgl",
        "MIT",
    ),
    package(
        "Noto Sans CJK SC",
        "SPDXRef-Package-Noto-Sans-CJK-SC",
        "NOASSERTION",
        "https://github.com/notofonts/noto-cjk",
        "OFL-1.1",
    ),
    package(
        "VirtualHere USB Server",
        "SPDXRef-Package-VirtualHere",
        "NOASSERTION",
        "https://www.virtualhere.com/usb_server_software",
        "LicenseRef-VirtualHere-Proprietary",
        supplier="Organization: VirtualHere Pty. Ltd.",
        checksum=sha256(virtualhere),
        comment=(
            "Proprietary redistributable is bundled by product decision. Public redistribution "
            "rights must be confirmed before publishing the GitHub Release."
        ),
    ),
]

document = {
    "spdxVersion": "SPDX-2.3",
    "dataLicense": "CC0-1.0",
    "SPDXID": "SPDXRef-DOCUMENT",
    "name": f"AirLink-{public_version}",
    "documentNamespace": (
        f"https://spdx.org/spdxdocs/AirLink-{public_version}-" + namespace_seed
    ),
    "creationInfo": {
        "creators": ["Tool: AirLink scripts/generate_sbom.py"],
        "created": datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "licenseListVersion": "3.25",
    },
    "documentDescribes": [item["SPDXID"] for item in packages],
    "packages": packages,
    "relationships": [
        {
            "spdxElementId": "SPDXRef-DOCUMENT",
            "relationshipType": "DESCRIBES",
            "relatedSpdxElement": item["SPDXID"],
        }
        for item in packages
    ],
    "hasExtractedLicensingInfos": [
        {
            "licenseId": "LicenseRef-VirtualHere-Proprietary",
            "name": "VirtualHere proprietary binary license",
            "extractedText": (
                "Proprietary software. See THIRD_PARTY_NOTICES.md and the vendor terms; "
                "no license grant is asserted by this SBOM."
            ),
        },
        {
            "licenseId": "LicenseRef-AIC8800-Firmware",
            "name": "AIC8800 firmware redistribution terms",
            "extractedText": (
                "Binary firmware supplied with the SG2002 vendor SDK. See the original vendor "
                "notices and confirm redistribution terms before publication."
            ),
        },
    ],
}

output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")