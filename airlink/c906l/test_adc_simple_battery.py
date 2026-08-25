#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parent
adc = (root / "adc1.c").read_text(encoding="utf-8")
hdr = (root / "adc1.h").read_text(encoding="utf-8")
ipc = (root / "ipc_smoke.c").read_text(encoding="utf-8")
ui = (root / "airlink_ui.c").read_text(encoding="utf-8")
build = (root / "build.sh").read_text(encoding="utf-8")

for marker in (
    "#define ADC_CALIBRATION_RAW         847U",
    "#define ADC_CALIBRATION_BATTERY_MV  4100U",
    "#define BATTERY_VALID_MIN_MV        2800U",
    "#define BATTERY_VALID_MAX_MV        4250U",
    "#define BATTERY_INVALID_CONFIRM_BATCHES 3U",
    "median3(status->raw_history[0]",
    "status->raw_history_count < 3U",
    "status->display_battery_millivolts = BATTERY_VALID_MAX_MV",
    "AIRLINK_ADC1_DISPLAY_CLAMP_HIGH",
    "airlink_adc1_note_failure(status)",
):
    assert marker in adc, marker

for field in (
    "raw_history[3]",
    "measurement_valid",
    "invalid_count",
    "invalid_streak",
    "display_valid",
    "display_source",
    "display_battery_millivolts",
):
    assert field in hdr, field

assert "adc1_status.display_battery_millivolts" in ipc
assert "adc1_status.ready && adc1_status.display_valid" in ipc
assert "adc1_status.ready && adc1_status.measurement_valid" not in ipc
assert "ADC1 display-source=" in ipc
assert "LVGL battery-start=C906L_FIRST_SAMPLE invalid-hold=3 clamp-high=4250mV" in ipc
assert "battery_level = 0U;\n        battery_level_initialized = 0U;" not in ui

main = ipc[ipc.index("void r25_main(void)"):]
assert main.index("adc1_start();") < main.index("display_start();") < main.index("ui_start();")
ui_start = ipc[ipc.index("static void ui_start(void)"):ipc.index("static void header_init")]
assert ui_start.index("ui_model_sync();") < ui_start.index("airlink_ui_init(")

serial = (
    "ADC1 filter=TRIMMED_MEAN+MEDIAN3 scaling=RAW_DIRECT "
    "raw847=4100mV valid=2800..4250mV"
)
assert serial in ipc and serial in build
assert "LVGL battery-start=C906L_FIRST_SAMPLE invalid-hold=3 clamp-high=4250mV" in build

def median3(a, b, c):
    return sorted((a, b, c))[1]

def battery_mv(raw):
    return (raw * 4100 + 847 // 2) // 847

assert median3(826, 992, 827) == 827
assert battery_mv(847) == 4100
assert all(battery_mv(raw) <= battery_mv(raw + 1) for raw in range(4095))

INITIAL, NORMAL, HELD, CLAMP_HIGH, INVALID = range(1, 6)

def update(display_mv, display_valid, invalid_streak, candidate, batch):
    if candidate is None or candidate < 2800:
        invalid_streak = min(3, invalid_streak + 1)
        if display_valid and invalid_streak < 3:
            return display_mv, True, invalid_streak, HELD
        return display_mv, False, invalid_streak, INVALID
    if candidate > 4250:
        return 4250, True, 0, CLAMP_HIGH
    return candidate, True, 0, INITIAL if batch == 1 else NORMAL

state = update(0, False, 0, 4100, 1)
assert state == (4100, True, 0, INITIAL)
state = update(*state[:3], None, 2)
assert state == (4100, True, 1, HELD)
state = update(*state[:3], None, 3)
assert state == (4100, True, 2, HELD)
state = update(*state[:3], None, 4)
assert state == (4100, False, 3, INVALID)
state = update(*state[:3], 4050, 5)
assert state == (4050, True, 0, NORMAL)
assert update(0, False, 0, 4800, 1) == (4250, True, 0, CLAMP_HIGH)

print("R27.6.6.23 ADC startup, clamp and invalid-hold tests: PASS")
