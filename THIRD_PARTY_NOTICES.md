# Third-party notices

This repository is a multi-license aggregate. Each third-party component keeps
its own license and copyright notices.

| Component | License/terms |
|---|---|
| Linux kernel | GPL-2.0-only and in-tree exceptions |
| U-Boot | GPL-2.0-or-later and directory-specific licenses |
| Buildroot | GPL-2.0-or-later; generated RootFS packages retain their licenses |
| OpenSBI | BSD-2-Clause |
| FreeRTOS | MIT and component-specific terms |
| LVGL 8.3.11 | MIT |
| Noto Sans SC | SIL Open Font License 1.1 |
| AIC8800 driver and firmware | Vendor/component-specific terms |
| VirtualHere USB Server | Proprietary VirtualHere license |

## VirtualHere

Bundled file:

```text
airlink/rootfs/usr/bin/vhusbdriscv64
SHA256 5a03e6dd928fa46b0aec1494919c42b67835cd2c2d3e3a5d27a563499b5b720c
```

Source page: https://www.virtualhere.com/usb_server_software

VirtualHere is not open-source software and is not covered by Apache-2.0. Its
embedded Linux license allows no-charge use for sharing one USB device and
contains restrictions including modification, reverse engineering, derivative
works, resale, transfer of rights and removal of proprietary notices.

The repository owner has chosen to integrate this exact binary so a clone can
build without a manual binary-copy step. This notice does not grant additional
VirtualHere rights and does not represent legal confirmation that public
redistribution is permitted for every use. Before making the repository or a
Release public, the publisher must confirm the applicable VirtualHere terms or
obtain written redistribution permission. If permission is not available, the
binary and affected Release assets must not be published.

## AIC8800 firmware

Buildroot downloads the fixed AIC8800 SDIO firmware snapshot whose archive hash
is recorded in the package hash file. The firmware archive does not contain a
clear redistributable open-source license. It is marked proprietary and
`REDISTRIBUTE = NO` in Buildroot and as `LicenseRef-AIC8800-Firmware` in the
SBOM. The publisher must independently confirm firmware redistribution rights
before publishing binaries that include it.
