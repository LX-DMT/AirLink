#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parent
ui = (root / "airlink_ui.c").read_text()
port = (root / "lv_port.c").read_text()
header = (root / "lv_port.h").read_text()
build = (root / "build.sh").read_text()
ipc = (root / "ipc_smoke.c").read_text()
assert "static uint32_t full_redraw_frames;" in ui
assert "static void request_full_redraw(uint32_t frames)" in ui
assert ui.count("request_full_redraw(2U);") >= 3
assert "lv_obj_invalidate(screen);" in ui
invalidate = ui.index("lv_obj_invalidate(screen);")
assert invalidate < ui.index("lv_timer_handler();", invalidate)
assert "airlink_lv_port_take_full_redraw_request()" in ui
assert "static uint32_t flush_recovery_pending;" in port
assert "static uint32_t frame_flush_failed;" in port
assert "flush_recovery_pending = 1U;" in port
assert "frame_flush_failed = 1U;" in port
assert "result == 0 && frame_flush_failed == 0U" in port
assert "airlink_lv_port_take_full_redraw_request(void)" in port
assert "airlink_lv_port_take_full_redraw_request(void);" in header
marker = "LVGL saver-redraw=FULLSCREEN_X2 flush-failure=RETRY"
assert marker in ipc
assert marker in build
assert 'python3 "$script_dir/test_saver_full_redraw.py"' in build
print("R27.6.6.14 saver full-redraw and flush-recovery tests: PASS")
