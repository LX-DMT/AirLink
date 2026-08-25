#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parent
ui = (root / "airlink_ui.c").read_text(encoding="utf-8")
ipc = (root / "ipc_smoke.c").read_text(encoding="utf-8")
build = (root / "build.sh").read_text(encoding="utf-8")
header = (root.parent / "ipc" / "airlink_ipc_v4.h").read_text(encoding="utf-8")
chars = (root / "airlink_fonts.chars.txt").read_text(encoding="utf-8")

start = ui.index("static void saver_update_mode(void)")
end = ui.index("static void enter_saver", start)
saver = ui[start:end]
page = ui[ui.index("static void page_update"):ui.index("static void request_full_redraw")]

for marker in (
    "AIRLINK_VIRTUALHERE_STOPPED",
    "AIRLINK_VIRTUALHERE_LISTENING",
    "AIRLINK_VIRTUALHERE_CLIENT_CONNECTED",
):
    assert marker in header and marker in ui, marker

for marker in (
    'lv_label_set_text(saver_state, "无线服务启动中")',
    'lv_label_set_text(saver_state, "等待电脑连接")',
    'lv_label_set_text(saver_state, "电脑已连接")',
    'lv_label_set_text(saver_state, "等待手机配网")',
    'lv_label_set_text(saver_state, "无线服务异常")',
):
    assert marker in saver, marker

for marker in (
    'lv_label_set_text(home_title, "无线服务启动中")',
    'lv_label_set_text(home_title, "等待电脑连接")',
    'lv_label_set_text(home_title, "电脑已连接")',
    'lv_label_set_text(home_subtitle, "无线共享已启动")',
    'lv_label_set_text(home_subtitle, "无线共享已就绪")',
):
    assert marker in page, marker

serial = ("LVGL saver-status=STATE_AWARE provision=WAIT_PHONE "
          "vh=STOPPED+LISTENING+CLIENT_CONNECTED")
assert serial in ipc and serial in build
assert 'python3 "$script_dir/test_saver_status.py"' in build

for text in ("无线服务启动中", "等待电脑连接", "电脑已连接", "无线共享已启动"):
    missing = [char for char in text if char not in chars]
    assert not missing, (text, missing)

STOPPED = 0
LISTENING = 1
CLIENT = 2

def wireless_status(vh_state):
    if vh_state == CLIENT:
        return "电脑已连接"
    if vh_state == LISTENING:
        return "等待电脑连接"
    return "无线服务启动中"

assert wireless_status(STOPPED) == "无线服务启动中"
assert wireless_status(LISTENING) == "等待电脑连接"
assert wireless_status(CLIENT) == "电脑已连接"
assert page.index('vh_state == AIRLINK_VIRTUALHERE_CLIENT_CONNECTED') < page.index(
    'vh_state == AIRLINK_VIRTUALHERE_LISTENING')
assert saver.index('vh_state == AIRLINK_VIRTUALHERE_CLIENT_CONNECTED') < saver.index(
    'vh_state == AIRLINK_VIRTUALHERE_LISTENING')
assert 'if (ui_stats.saver)\n                saver_update_mode();' in ui
print("R27.6.6.23 VirtualHere three-state home/saver tests: PASS")
