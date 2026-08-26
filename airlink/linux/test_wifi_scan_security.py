#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parent
source = (root / "airlink_provision.c").read_text(encoding="utf-8")
header = (root / "airlink_provision.h").read_text(encoding="utf-8")

assert "uint32_t security_known;" in header
parser = source[source.index("static void scan_parse_security"):
                source.index("static void scan_parse(")]
assert '"capability:"' in parser
assert '"Privacy"' in parser
assert all(marker in parser for marker in ('"RSN:"', '"WPA:"', '"WEP:"'))
addnet = source[source.index("static void addnet"):
                source.index("static int cmpnet")]
assert "if (!network->security_known)" in addnet
assert "network->secured = 1U;" in addnet
assert "stored->secured || network->secured" in addnet
assert "stored->security_known || network->security_known" in addnet
assert "scan_parse_security(&network, cursor);" in source
for sample in (
    "capability: ESS Privacy ShortSlotTime (0x0411)",
    "capability: ESS ShortSlotTime (0x0401)",
):
    assert sample in source
print("Wi-Fi scan security classification tests: PASS")
