#!/usr/bin/env python3
"""Replace one FIT FDT data property and its SHA-256 hash.

The script is intentionally self-contained because the recovery host may not
have mkimage/dumpimage or Python libfdt installed. It preserves every FIT node
and property, replacing only:

  /images/<fdt-node>/data
  /images/<fdt-node>/hash-1/value
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

FDT_MAGIC = 0xD00DFEED
FDT_BEGIN_NODE = 1
FDT_END_NODE = 2
FDT_PROP = 3
FDT_NOP = 4
FDT_END = 9
HEADER = struct.Struct(">10I")


def align4(value: int) -> int:
    return (value + 3) & ~3


def c_string(blob: bytes, offset: int) -> str:
    end = blob.find(b"\0", offset)
    if end < 0:
        raise ValueError("unterminated string")
    return blob[offset:end].decode("ascii")


class FitImage:
    def __init__(self, blob: bytes):
        if len(blob) < HEADER.size:
            raise ValueError("input is smaller than an FDT header")
        fields = HEADER.unpack_from(blob)
        (
            self.magic,
            self.total_size,
            self.off_struct,
            self.off_strings,
            self.off_reserve,
            self.version,
            self.last_compatible_version,
            self.boot_cpuid,
            self.size_strings,
            self.size_struct,
        ) = fields
        if self.magic != FDT_MAGIC:
            raise ValueError(f"bad FDT magic: 0x{self.magic:08x}")
        if self.total_size > len(blob):
            raise ValueError("FDT total size exceeds input length")
        self.prefix = blob[HEADER.size : self.off_struct]
        self.structure = blob[
            self.off_struct : self.off_struct + self.size_struct
        ]
        self.strings = blob[
            self.off_strings : self.off_strings + self.size_strings
        ]

    def transform(self, replacements: dict[tuple[str, str], bytes]) -> tuple[bytes, dict]:
        source = self.structure
        output = bytearray()
        stack: list[str] = []
        seen: dict[str, dict[str, bytes]] = {}
        offset = 0

        while offset + 4 <= len(source):
            token = struct.unpack_from(">I", source, offset)[0]
            offset += 4
            output += struct.pack(">I", token)

            if token == FDT_BEGIN_NODE:
                end = source.find(b"\0", offset)
                if end < 0:
                    raise ValueError("unterminated node name")
                raw_name = source[offset : end + 1]
                padded_end = align4(end + 1)
                output += source[offset:padded_end]
                stack.append(raw_name[:-1].decode("ascii"))
                offset = padded_end
            elif token == FDT_END_NODE:
                if not stack:
                    raise ValueError("END_NODE without BEGIN_NODE")
                stack.pop()
            elif token == FDT_PROP:
                if offset + 8 > len(source):
                    raise ValueError("truncated property header")
                length, name_offset = struct.unpack_from(">II", source, offset)
                offset += 8
                data_end = offset + length
                padded_end = align4(data_end)
                if padded_end > len(source):
                    raise ValueError("truncated property data")
                prop_name = c_string(self.strings, name_offset)
                path = "/" + "/".join(part for part in stack if part)
                old_value = source[offset:data_end]
                new_value = replacements.get((path, prop_name), old_value)
                output += struct.pack(">II", len(new_value), name_offset)
                output += new_value
                output += bytes(align4(len(new_value)) - len(new_value))
                seen.setdefault(path, {})[prop_name] = bytes(new_value)
                offset = padded_end
            elif token == FDT_NOP:
                pass
            elif token == FDT_END:
                break
            else:
                raise ValueError(f"unknown FDT token 0x{token:08x}")
        else:
            raise ValueError("FDT_END token not found")

        new_structure = bytes(output)
        off_struct = HEADER.size + len(self.prefix)
        off_strings = align4(off_struct + len(new_structure))
        between = bytes(off_strings - (off_struct + len(new_structure)))
        total_size = off_strings + len(self.strings)
        header = HEADER.pack(
            self.magic,
            total_size,
            off_struct,
            off_strings,
            self.off_reserve,
            self.version,
            self.last_compatible_version,
            self.boot_cpuid,
            len(self.strings),
            len(new_structure),
        )
        result = header + self.prefix + new_structure + between + self.strings
        if len(result) != total_size:
            raise AssertionError("rebuilt FIT length does not match header")
        return result, seen


def get_property(blob: bytes, path: str, name: str) -> bytes:
    fit = FitImage(blob)
    _unused, seen = fit.transform({})
    try:
        return seen[path][name]
    except KeyError as exc:
        raise ValueError(f"missing FIT property {path}/{name}") from exc


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input_fit", type=Path)
    parser.add_argument("replacement_dtb", type=Path)
    parser.add_argument("output_fit", type=Path)
    parser.add_argument(
        "--node",
        default="fdt-sg2002_licheervnano_sd",
        help="FIT image node name under /images",
    )
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    original = args.input_fit.read_bytes()
    replacement_dtb = args.replacement_dtb.read_bytes()
    data_path = f"/images/{args.node}"
    hash_path = f"{data_path}/hash-1"

    old_fdt = get_property(original, data_path, "data")
    old_hash = get_property(original, hash_path, "value")
    algorithm = get_property(original, hash_path, "algo").rstrip(b"\0")
    if algorithm != b"sha256":
        raise SystemExit(f"unsupported FIT hash algorithm: {algorithm!r}")
    if old_hash != hashlib.sha256(old_fdt).digest():
        raise SystemExit("original FIT FDT hash does not match its data")

    new_hash = hashlib.sha256(replacement_dtb).digest()
    replacements = {
        (data_path, "data"): replacement_dtb,
        (hash_path, "value"): new_hash,
    }
    rebuilt, seen = FitImage(original).transform(replacements)

    if seen.get(data_path, {}).get("data") != replacement_dtb:
        raise SystemExit("replacement FDT property was not found")
    if seen.get(hash_path, {}).get("value") != new_hash:
        raise SystemExit("replacement hash property was not found")

    args.output_fit.parent.mkdir(parents=True, exist_ok=True)
    args.output_fit.write_bytes(rebuilt)

    # Verify the serialized output by parsing it again.
    if get_property(rebuilt, data_path, "data") != replacement_dtb:
        raise SystemExit("serialized FIT does not contain replacement DTB")
    if get_property(rebuilt, hash_path, "value") != new_hash:
        raise SystemExit("serialized FIT does not contain replacement hash")

    report = {
        "input_fit": str(args.input_fit),
        "output_fit": str(args.output_fit),
        "fit_node": data_path,
        "hash_node": hash_path,
        "hash_algorithm": "sha256",
        "input_fit_size": len(original),
        "output_fit_size": len(rebuilt),
        "old_dtb_size": len(old_fdt),
        "new_dtb_size": len(replacement_dtb),
        "old_dtb_sha256": hashlib.sha256(old_fdt).hexdigest(),
        "new_dtb_sha256": new_hash.hex(),
        "old_fit_sha256": hashlib.sha256(original).hexdigest(),
        "new_fit_sha256": hashlib.sha256(rebuilt).hexdigest(),
    }
    text = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.report:
        args.report.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
