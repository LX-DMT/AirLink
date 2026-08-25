#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parent
ui = (root / "airlink_ui.c").read_text(encoding="utf-8")
ipc = (root / "ipc_smoke.c").read_text(encoding="utf-8")
build = (root / "build.sh").read_text(encoding="utf-8")

for marker in (
    "static lv_obj_t *status_battery_cells[4]",
    "static lv_obj_t *battery_cells[4]",
    "static uint32_t battery_level_direct",
    "{3350U, 3550U, 3750U, 3950U}",
    "thresholds[battery_level] + 20U",
    "current.battery_mv + 20U < thresholds[battery_level - 1U]",
    "battery_level <= 1U ? C_AMBER : C_GREEN",
    'lv_label_set_text(battery_voltage, "-- V")',
    "lv_obj_t *battery = plain(pages[3], 51, 78, 132, 62)",
    "lv_obj_t *terminal = plain(pages[3], 183, 97, 8, 24)",
    "C_TEXT, 37, 158, 166",
    "*pointer++ = (char)('0' + fraction / 10U)",
    "*pointer++ = (char)('0' + fraction % 10U)",
):
    assert marker in ui, marker

for obsolete in (
    "status_battery_text",
    "status_battery_fill",
    "battery_arc",
    "battery_value",
    "battery_kicker",
    "current.battery_percent",
    '"电量偏低"',
    '"电量正常"',
    '"电池采样异常"',
    '"--%"',
    '"82%"',
):
    assert obsolete not in ui, obsolete

serial = (
    "LVGL battery-ui=4SEG percent=REMOVED status-text=REMOVED "
    "layout=CENTERED voltage=2DP hysteresis=20mV invalid=--V"
)
assert serial in ipc and serial in build
assert 'python3 "$script_dir/test_battery_bars.py"' in build
assert "if (!current.battery_valid)\n        return;" in ui
assert "battery_level = 0U;\n        battery_level_initialized = 0U;" not in ui

def direct(mv):
    if mv >= 3950:
        return 4
    if mv >= 3750:
        return 3
    if mv >= 3550:
        return 2
    if mv >= 3350:
        return 1
    return 0

assert [(mv, direct(mv)) for mv in (3349, 3350, 3549, 3550, 3749, 3750, 3949, 3950)] == [
    (3349, 0), (3350, 1), (3549, 1), (3550, 2),
    (3749, 2), (3750, 3), (3949, 3), (3950, 4),
]

def update(level, mv):
    thresholds = (3350, 3550, 3750, 3950)
    while level < 4 and mv >= thresholds[level] + 20:
        level += 1
    while level > 0 and mv + 20 < thresholds[level - 1]:
        level -= 1
    return level

assert update(1, 3569) == 1
assert update(1, 3570) == 2
assert update(2, 3531) == 2
assert update(2, 3529) == 1
assert update(3, 3970) == 4
assert update(4, 3931) == 4
assert update(4, 3929) == 3

def voltage_text(mv):
    fraction = (mv % 1000) // 10
    return f"{mv // 1000}.{fraction:02d} V"

assert voltage_text(4000) == "4.00 V"
assert voltage_text(4100) == "4.10 V"
assert voltage_text(3987) == "3.98 V"

print("R27.6.4 four-segment battery UI and hysteresis tests: PASS")
