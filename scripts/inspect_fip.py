#!/usr/bin/env python3
"""Inspect a CVITEK FIP image and emit deterministic component manifests."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
from typing import Any, Dict, Optional


REPO_ROOT = Path(__file__).resolve().parents[1]
FIPTOOL_PATH = REPO_ROOT / "fsbl" / "plat" / "cv181x" / "fiptool.py"


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_fiptool() -> Any:
    spec = importlib.util.spec_from_file_location("airlink_cvitek_fiptool", FIPTOOL_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load FIP tool: {FIPTOOL_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def int_value(entries: Any, name: str) -> int:
    return entries[name].toint()


def hex_value(entries: Any, name: str) -> str:
    return bytes(entries[name].content).hex()


def component(
    name: str,
    data: bytes,
    offset: Optional[int],
    run_address: Optional[int] = None,
) -> Dict[str, Any]:
    size = len(data)
    item: Dict[str, Any] = {
        "name": name,
        "present": size > 0,
        "size": size,
        "offset": offset,
        "end_offset": offset + size if offset is not None else None,
        "sha256": sha256_bytes(data) if size else None,
    }
    if run_address is not None:
        item["run_address"] = run_address
    return item


def inspect(path: Path, source_label: Optional[str] = None) -> Dict[str, Any]:
    tool = load_fiptool()
    fip = tool.FIP()
    fip.read_fip(str(path))

    blcp = bytes(fip.body1["BLCP"].content)
    bl2 = bytes(fip.body1["BL2"].content)
    ddr_param = bytes(fip.body2["DDR_PARAM"].content)
    blcp_2nd = bytes(fip.body2["BLCP_2ND"].content)
    monitor = bytes(fip.body2["MONITOR"].content)
    loader_2nd = bytes(fip.body2["LOADER_2ND"].content)

    blcp_offset = tool.PARAM1_SIZE if blcp else None
    bl2_offset = tool.PARAM1_SIZE + len(blcp) if bl2 else None

    components = [
        component(
            "BLCP",
            blcp,
            blcp_offset,
            int_value(fip.param1, "BLCP_IMG_RUNADDR"),
        ),
        component("BL2", bl2, bl2_offset),
        component(
            "DDR_PARAM",
            ddr_param,
            int_value(fip.param2, "DDR_PARAM_LOADADDR") if ddr_param else None,
        ),
        component(
            "BLCP_2ND",
            blcp_2nd,
            int_value(fip.param2, "BLCP_2ND_LOADADDR") if blcp_2nd else None,
            int_value(fip.param2, "BLCP_2ND_RUNADDR"),
        ),
        component(
            "MONITOR",
            monitor,
            int_value(fip.param2, "MONITOR_LOADADDR") if monitor else None,
            int_value(fip.param2, "MONITOR_RUNADDR"),
        ),
        component(
            "LOADER_2ND",
            loader_2nd,
            int_value(fip.param2, "LOADER_2ND_LOADADDR") if loader_2nd else None,
            int_value(fip.ldr_2nd_hdr, "RUNADDR") if loader_2nd else None,
        ),
    ]

    loader_magic = (
        bytes(fip.ldr_2nd_hdr["MAGIC"].content).decode("ascii", errors="replace")
        if loader_2nd
        else None
    )

    return {
        "format": "cvitek-fip",
        "source": source_label or str(path.resolve()),
        "size": path.stat().st_size,
        "sha256": sha256_file(path),
        "header": {
            "param2_offset": int_value(fip.param1, "PARAM2_LOADADDR"),
            "fip_flags": int_value(fip.param1, "FIP_FLAGS"),
            "bl2_checksum": hex_value(fip.param1, "BL2_IMG_CKSUM"),
            "ddr_param_checksum": hex_value(fip.param2, "DDR_PARAM_CKSUM"),
            "blcp_2nd_checksum": hex_value(fip.param2, "BLCP_2ND_CKSUM"),
            "monitor_checksum": hex_value(fip.param2, "MONITOR_CKSUM"),
            "loader_2nd_magic": loader_magic,
            "loader_2nd_header_size": (
                int_value(fip.ldr_2nd_hdr, "SIZE") if loader_2nd else 0
            ),
        },
        "components": components,
        "unparsed_trailing_bytes": len(getattr(fip, "rest_fip", b"")),
    }


def format_address(value: Optional[int]) -> str:
    return "-" if value is None else f"0x{value:08x}"


def render_text(report: Dict[str, Any]) -> str:
    lines = [
        "AirLink CVITEK FIP component manifest",
        f"source: {report['source']}",
        f"size: {report['size']}",
        f"sha256: {report['sha256']}",
        f"param2_offset: {format_address(report['header']['param2_offset'])}",
        f"fip_flags: 0x{report['header']['fip_flags']:x}",
        f"loader_2nd_magic: {report['header']['loader_2nd_magic'] or '-'}",
        f"unparsed_trailing_bytes: {report['unparsed_trailing_bytes']}",
        "",
        "NAME        PRESENT  OFFSET      SIZE       RUNADDR     SHA256",
    ]
    for item in report["components"]:
        lines.append(
            f"{item['name']:<11} "
            f"{str(item['present']).lower():<8} "
            f"{format_address(item['offset']):<11} "
            f"{item['size']:<10} "
            f"{format_address(item.get('run_address')):<11} "
            f"{item['sha256'] or '-'}"
        )
    lines.append("")
    lines.extend(
        [
            f"BL2 checksum field: {report['header']['bl2_checksum']}",
            f"DDR_PARAM checksum field: {report['header']['ddr_param_checksum']}",
            f"BLCP_2ND checksum field: {report['header']['blcp_2nd_checksum']}",
            f"MONITOR checksum field: {report['header']['monitor_checksum']}",
        ]
    )
    return "\n".join(lines) + "\n"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fip", type=Path, help="FIP binary to inspect")
    parser.add_argument("--json-out", type=Path, help="write JSON manifest")
    parser.add_argument("--text-out", type=Path, help="write text manifest")
    parser.add_argument(
        "--source-label",
        help="logical source name to record instead of the temporary file path",
    )
    parser.add_argument(
        "--format",
        choices=("text", "json"),
        default="text",
        help="stdout format",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    report = inspect(args.fip, args.source_label)
    text = render_text(report)
    json_text = json.dumps(report, indent=2, sort_keys=True) + "\n"

    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json_text, encoding="utf-8")
    if args.text_out:
        args.text_out.parent.mkdir(parents=True, exist_ok=True)
        args.text_out.write_text(text, encoding="utf-8")

    print(json_text if args.format == "json" else text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
