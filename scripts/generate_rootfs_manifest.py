#!/usr/bin/env python3
from pathlib import Path
import hashlib
import os
import stat
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: generate_rootfs_manifest.py ROOT OUTPUT")
root = Path(sys.argv[1]).resolve()
output = Path(sys.argv[2])
if root == Path("/") or not (root / "etc/init.d").is_dir():
    raise SystemExit("unsafe RootFS")

lines = ["# AirLink immutable RootFS manifest", ""]
for path in sorted(root.rglob("*")):
    rel = "/" + path.relative_to(root).as_posix()
    st = path.lstat()
    mode = stat.S_IMODE(st.st_mode)
    if path.is_symlink():
        lines.append(f"symlink {mode:04o} {rel} -> {os.readlink(path)}")
    elif path.is_file():
        h = hashlib.sha256()
        with path.open("rb") as handle:
            for block in iter(lambda: handle.read(1024 * 1024), b""):
                h.update(block)
        lines.append(f"{h.hexdigest()} {mode:04o} {st.st_size:12d} {rel}")
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text("\n".join(lines) + "\n", encoding="utf-8")
