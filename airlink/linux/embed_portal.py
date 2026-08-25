#!/usr/bin/env python3
import argparse
from pathlib import Path

def c_string(data: bytes) -> str:
    lines = ["/* Generated from airlink_portal.html. Do not edit. */",
             "static const char airlink_portal_html[] ="]
    width = 72
    escaped = []
    for byte in data:
        if byte == 34:
            escaped.append(r'\"')
        elif byte == 92:
            escaped.append(r'\\')
        elif byte == 10:
            escaped.append(r'\n')
        elif byte == 13:
            escaped.append(r'\r')
        elif byte == 9:
            escaped.append(r'\t')
        elif 32 <= byte <= 126:
            escaped.append(chr(byte))
        else:
            escaped.append(f"\\{byte:03o}")
    current = ""
    for token in escaped:
        if len(current) + len(token) > width:
            lines.append(f'"{current}"')
            current = ""
        current += token
    lines.append(f'"{current}";')
    return "\n".join(lines) + "\n"

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("html")
    parser.add_argument("output")
    args = parser.parse_args()
    source = Path(args.html).read_bytes()
    if b"\x00" in source:
        raise SystemExit("portal HTML contains NUL")
    if len(source) > 32768:
        raise SystemExit(f"portal HTML too large: {len(source)}")
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(c_string(source), encoding="ascii")
    print(f"Embedded portal HTML: {len(source)} bytes -> {output}")

if __name__ == "__main__":
    main()
