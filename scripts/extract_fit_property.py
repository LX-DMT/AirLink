#!/usr/bin/env python3
"""Extract one binary property from an FDT/FIT without libfdt."""

from __future__ import annotations

import argparse
from pathlib import Path

from patch_fit_fdt import get_property


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input_fit", type=Path)
    parser.add_argument("node_path")
    parser.add_argument("property_name")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    value = get_property(
        args.input_fit.read_bytes(), args.node_path, args.property_name
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(value)
    print(f"{args.node_path}/{args.property_name}: {len(value)} bytes -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
