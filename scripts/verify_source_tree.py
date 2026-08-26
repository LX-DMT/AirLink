#!/usr/bin/env python3
from pathlib import Path
import hashlib
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
errors = []
required = [
    "Makefile", "versions.lock", "LICENSE", "README.md", "README.zh-CN.md",
    "airlink/c906l/build.sh", "airlink/c906l/ipc_smoke.c",
    "airlink/linux/build_linux.sh", "airlink/linux/airlinkd.c",
    "airlink/ipc/airlink_ipc_v4.h", "airlink/portal/index.html",
    "airlink/rootfs/usr/bin/vhusbdriscv64",
    "airlink/rootfs/etc/init.d/S25wifimod",
    "buildroot/package/aic8800-sdio-firmware/aic8800-sdio-firmware.mk",
    "build/cvisetup.sh", "linux_5.10/Makefile",
    "osdrv/interdrv/v2/jpeg/mars/soph_jpeg.ko",
    "osdrv/interdrv/v2/jpeg/mars_arm/soph_jpeg.ko",
    "osdrv/interdrv/v2/jpeg/mars_riscv/soph_jpeg.ko",
    "osdrv/interdrv/v2/jpeg/phobos/soph_jpeg.ko",
    "osdrv/interdrv/v2/jpeg/phobos_riscv/soph_jpeg.ko",
    "osdrv/interdrv/v2/cvi_vc_drv/mars/soph_vc_driver.ko",
    "osdrv/interdrv/v2/cvi_vc_drv/mars_arm/soph_vc_driver.ko",
    "osdrv/interdrv/v2/cvi_vc_drv/mars_riscv/soph_vc_driver.ko",
    "osdrv/interdrv/v2/cvi_vc_drv/phobos/soph_vc_driver.ko",
    "osdrv/interdrv/v2/cvi_vc_drv/phobos_riscv/soph_vc_driver.ko",
    "scripts/release.sh", "scripts/release_inner.sh",
    "scripts/generate_validation_evidence.sh", "scripts/version_info.py",
    "docs/zh-CN/architecture.md", "docs/en/architecture.md",
    "build/boards/sg200x/sg2002_licheervnano_sd/dts_riscv/sg2002_licheervnano_sd.dts",
]
for rel in required:
    if not (root / rel).exists():
        errors.append(f"missing required path: {rel}")

for rel in (".gitmodules", "tools/airlink_recovery", "recovery"):
    if (root / rel).exists():
        errors.append(f"forbidden historical/submodule path: {rel}")

# Reject hidden nested repositories even when their .git directory would not
# appear in git ls-files. Generated SDK/cache directories are pruned.
for current, directories, _files in os.walk(root):
    current_path = Path(current)
    if current_path == root:
        directories[:] = [
            name for name in directories
            if name not in {".git", ".cache", "host-tools", "install", "out"}
        ]
    if current_path == root / "buildroot":
        directories[:] = [
            name for name in directories if name not in {"dl", "output"}
        ]
    if ".git" in directories:
        nested = (current_path / ".git").relative_to(root)
        errors.append(f"nested Git repository metadata: {nested}")
        directories.remove(".git")

tracked_or_publishable = subprocess.run(
    ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
    cwd=root, check=True, stdout=subprocess.PIPE,
).stdout.decode("utf-8", errors="surrogateescape").split("\0")
staged = subprocess.run(
    ["git", "ls-files", "-s", "-z"],
    cwd=root, check=True, stdout=subprocess.PIPE,
).stdout.decode("utf-8", errors="surrogateescape").split("\0")
for entry in staged:
    if entry.startswith("160000 "):
        errors.append(f"Git submodule entry remains: {entry.split(chr(9), 1)[-1]}")

for rel in tracked_or_publishable:
    if not rel:
        continue
    path = root / rel
    if path.name == ".gitmodules":
        errors.append(f"nested submodule declaration: {rel}")
    if path.is_file() and path.stat().st_size >= 100 * 1024 * 1024:
        errors.append(f"GitHub 100 MiB file limit exceeded: {rel}")

# The vendor SDK intentionally contains precompiled Python bytecode in the
# target root filesystem. Reject tracked or otherwise publishable caches in
# AirLink-owned trees, but ignore normal build-time caches covered by
# .gitignore so running the documented commands remains repeatable.
publishable = {rel for rel in tracked_or_publishable if rel}
for rel in sorted(publishable):
    path = Path(rel)
    if path.parts and path.parts[0] in {"airlink", "scripts", "docs"}:
        if "__pycache__" in path.parts or path.suffix in (".pyc", ".pyo"):
            errors.append(f"Python cache would be published: {rel}")

scan_ext = {".sh", ".py", ".c", ".h", ".md", ".yml", ".yaml"}
absolute = re.compile(r"/home/pr/|/mnt/c/Users/70918|C:\\\\Users\\\\70918", re.I)
private_key = re.compile(r"BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY")
for base in (root / "airlink", root / "scripts", root / "docs"):
    for path in base.rglob("*"):
        if not path.is_file() or path.suffix.lower() not in scan_ext:
            continue
        if path.name in {"doctor.sh", "verify_source_tree.py"}:
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        if absolute.search(text):
            errors.append(f"developer path remains: {path.relative_to(root)}")
        if private_key.search(text):
            errors.append(f"private key remains: {path.relative_to(root)}")

vh = root / "airlink/rootfs/usr/bin/vhusbdriscv64"
if vh.exists():
    digest = hashlib.sha256(vh.read_bytes()).hexdigest()
    expected = "5a03e6dd928fa46b0aec1494919c42b67835cd2c2d3e3a5d27a563499b5b720c"
    if digest != expected:
        errors.append(f"VirtualHere SHA256 mismatch: {digest}")

dts = (root / required[-1]).read_text(encoding="utf-8")
for marker in (
    '&spi0 {',
    '&i2c4 {',
    '&{/saradc} {',
    '&mipi_tx {',
    '&cvi_vo {',
):
    if marker not in dts:
        errors.append(f"final DTB ownership marker missing: {marker}")
for node in ("spi4: spi4@gpio", "i2c5: i2c5@gpio"):
    if node in dts:
        errors.append(f"legacy GPIO bus still enabled: {node}")

disabled_nodes = (
    "i2c0", "i2c1", "i2c2", "i2c3", "i2c4",
    "uart1", "uart2", "uart3",
    "pwm0", "pwm1", "pwm2",
    "spi0", "dac", "mipi_rx",
)
for node in disabled_nodes:
    pattern = rf"&{re.escape(node)}\s*\{{.*?status\s*=\s*\"disabled\";"
    if not re.search(pattern, dts, re.S):
        errors.append(f"Linux ownership node is not disabled: {node}")
for node in ("mipi_tx", "cvi_vo"):
    pattern = (
        rf"&{node}\s*\{{.*?#ifndef __UBOOT__.*?"
        rf"status\s*=\s*\"disabled\";"
    )
    if not re.search(pattern, dts, re.S):
        errors.append(f"Linux display conflict is not disabled: {node}")

board_defconfig = (
    root / "build/boards/sg200x/sg2002_licheervnano_sd/sg2002_licheervnano_sd_defconfig"
).read_text(encoding="utf-8")
if "CONFIG_MIPI_PANEL_ZCT2133V1=y" in board_defconfig:
    errors.append("U-Boot MIPI panel must remain disabled; C906L owns the round display")

uboot_defconfig = (
    root / "build/boards/sg200x/sg2002_licheervnano_sd/u-boot/sg2002_licheervnano_sd_defconfig"
).read_text(encoding="utf-8")
for option in (
    "CONFIG_DISPLAY=y",
    "CONFIG_DM_VIDEO=y",
    "CONFIG_VIDEO_CVITEK=y",
    "CONFIG_DISPLAY_CVITEK_MIPI=y",
    "CONFIG_BOOTLOGO=y",
    "CONFIG_CMD_CVI_VO=y",
):
    if option in uboot_defconfig:
        errors.append(f"U-Boot display ownership option must remain disabled: {option}")

fdrv_makefile = (
    root / "osdrv/extdrv/wireless/aic8800/aic8800_fdrv/Makefile"
).read_text(encoding="utf-8")
if not re.search(r"^CONFIG_SDIO_BT\s*=\s*n\s*$", fdrv_makefile, re.M):
    errors.append("AIC8800 fdrv must keep CONFIG_SDIO_BT=n")

firmware_makefile = (
    root / "buildroot/package/aic8800-sdio-firmware/aic8800-sdio-firmware.mk"
).read_text(encoding="utf-8")
firmware_link = re.compile(
    r"^[ \t]*ln\s+-sfn\s+aic8800_and_aic8800D80\s+"
    r"\$\(TARGET_DIR\)/usr/lib/firmware/aic8800_sdio/aic8800[ \t]*$",
    re.M,
)
if not firmware_link.search(firmware_makefile):
    errors.append("AIC8800 firmware compatibility symlink install rule missing")

if errors:
    print("\n".join("ERROR: " + item for item in errors), file=sys.stderr)
    raise SystemExit(1)
print("AirLink source tree verification: PASS")
