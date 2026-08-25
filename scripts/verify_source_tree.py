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
    "build/cvisetup.sh", "linux_5.10/Makefile",
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
for marker in ('&spi0 {\n\tstatus = "disabled";',
               '&i2c4 {\n        status = "disabled";',
               '&{/saradc} {\n        status = "disabled";'):
    if marker not in dts:
        errors.append(f"final DTB ownership marker missing: {marker}")

if errors:
    print("\n".join("ERROR: " + item for item in errors), file=sys.stderr)
    raise SystemExit(1)
print("AirLink source tree verification: PASS")
