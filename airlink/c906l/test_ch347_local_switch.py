#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parent
ui = (root / "airlink_ui.c").read_text(encoding="utf-8")
ipc = (root / "ipc_smoke.c").read_text(encoding="utf-8")
build = (root / "build.sh").read_text(encoding="utf-8")

for forbidden in (
    "CHSM_WAIT_PREPARED", "CHSM_WAIT_ENUM", "ch347_request_sequence",
    "ch347_deadline", "CH347 prepare=REQUEST", "CH347 restart=REQUEST",
    "CH347 prepare=TIMEOUT", "CH347 restart=TIMEOUT",
):
    assert forbidden not in ipc, forbidden

request = ipc[ipc.index("static void ch347_request_switch"):
              ipc.index("static void ch347_state_machine")]
for marker in (
    "ch347_sm_state != CHSM_IDLE",
    "ch347_begin_electrical(now);",
):
    assert marker in request, marker
for forbidden in (
    "linux_peer_seen", "stable_level", "send_message",
    "AIRLINK_IPC_MSG_CH347_SWITCH_REQUEST",
):
    assert forbidden not in request, forbidden

state = ipc[ipc.index("static void ch347_state_machine"):
            ipc.index("static void send_mode_event")]
for marker in (
    "airlink_ch347_tick(&ch347_status, now)",
    "CH347 local-switch reset=80ms PASS elapsed_ms=",
    "ch347_finish_ui(1U)",
):
    assert marker in state, marker
for forbidden in ("send_message", "WAIT_PREPARED", "WAIT_ENUM"):
    assert forbidden not in state, forbidden

begin = ipc[ipc.index("static void ch347_begin_electrical"):
            ipc.index("static void ch347_request_switch")]
for marker in (
    "airlink_ch347_begin_mode(&ch347_status, ch347_pending_mode, now)",
    "ch347_switch_started = now;",
    "CH347 local-switch mode=",
    'uart_puts(" dtr1=")',
    'uart_puts(" rts1=")',
):
    assert marker in begin, marker
assert "airlink_ch347_apply_mode" not in ipc

assert '"系统切换中"' not in ui
controls = ui[ui.index("static void mode_controls_update"):
              ui.index("static void page_update")]
for marker in (
    "uint32_t system_locked = mode_controls_locked();",
    "uint32_t ch347_locked = ch347_controls_locked();",
    'ch347_locked ? "切换中" : "切换模式"',
    'system_locked ? "暂不可用" : "重新配置"',
):
    assert marker in controls, marker

touch = ui[ui.index("} else if (overlay == OVERLAY_NONE && !hold_sent && ax < 18"):
           ui.index("    if (pending_event != 0U)")]
assert "page_index == 1U && !ch347_controls_locked()" in touch
assert "page_index == 2U && !current.wired" in touch
assert "!mode_controls_locked()" in touch

serial = (
    "LVGL ch347-control=LOCAL_ALWAYS system-lock=REMOVED "
    "ownership-monitor=PAUSED_DURING_RESET readback=AUTO_RECOVER"
)
assert serial in ipc
assert serial in build
for marker in (
    "CH347 local-switch mode=",
    "CH347 local-switch reset=80ms PASS elapsed_ms=",
):
    assert marker in build

print("R27.5 CH347 local-always nonblocking switch tests: PASS")
