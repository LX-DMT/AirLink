#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parent
driver = (root / "ch347.c").read_text(encoding="utf-8")
header = (root / "ch347.h").read_text(encoding="utf-8")
ipc = (root / "ipc_smoke.c").read_text(encoding="utf-8")
build = (root / "build.sh").read_text(encoding="utf-8")

begin = driver[driver.index("int airlink_ch347_begin_mode"):
               driver.index("int airlink_ch347_tick")]
assert begin.index("status->ready = 0U;") < begin.index(
    "output = mmio_read(GPIO_OUT) & ~CH347_RESET_MASK;")
assert "status->last_switch_mismatch = 0U;" in begin
assert "status->switching = 1U;" in begin

tick = driver[driver.index("int airlink_ch347_tick"):
              driver.index("int airlink_ch347_apply_mode")]
for marker in (
    "status->last_switch_mismatch = status->ownership_mismatch;",
    "claim_pins(status->pending_mode);",
    "status->switch_recovery_count++;",
    "status->recovery_count++;",
    "status->switching = 0U;",
    "status->ready = 1U;",
):
    assert marker in tick, marker
assert tick.index("claim_pins(status->pending_mode);") < tick.index(
    "status->error_flags |= AIRLINK_CH347_ERROR_OUTPUT_READBACK;")
assert tick.index("status->switch_count++;") > tick.index(
    "if (status->ownership_mismatch != 0U)")

ownership = driver[driver.index("int airlink_ch347_check_ownership"):
                   driver.index("const char *airlink_ch347_mode_name")]
assert "if (status->switching || !status->ready)" in ownership
assert ownership.index("if (status->switching || !status->ready)") <        ownership.index("capture_status(status);")

for marker in (
    "uint32_t last_switch_mismatch;",
    "uint32_t switch_recovery_count;",
):
    assert marker in header, marker

monitor = ipc[ipc.index("static void check_gpio_ownership"):
              ipc.index("void r25_main")]
for marker in (
    "ch347_sm_state == CHSM_IDLE",
    "ch347_status.ready && !ch347_status.switching",
):
    assert marker in monitor, marker
assert monitor.index("ch347_sm_state == CHSM_IDLE") < monitor.index(
    "airlink_ch347_check_ownership(&ch347_status)")

state = ipc[ipc.index("static void ch347_state_machine"):
            ipc.index("static void send_mode_event")]
for marker in (
    "CH347 local-switch readback=RECOVERED mismatch=0x",
    "ch347_status.switch_recovery_count",
    'uart_puts(" mismatch=0x")',
    "ch347_status.ownership_mismatch",
):
    assert marker in state, marker

serial = (
    "LVGL ch347-control=LOCAL_ALWAYS system-lock=REMOVED "
    "ownership-monitor=PAUSED_DURING_RESET readback=AUTO_RECOVER"
)
assert serial in ipc
assert serial in build
assert "python3 \"$script_dir/test_ch347_switch_race.py\"" in build

print("R27.6.6.23 CH347 reset/ownership race regression tests: PASS")