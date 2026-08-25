#!/usr/bin/env python3
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parent
meta = json.loads((root / "airlink_fonts.json").read_text(encoding="utf-8"))
assert meta["family"] == "Noto Sans SC"
assert meta["source_sha256"] == "763146584cf0710223441356b4395e279021b0806c196614377a7a0174ae074a"
assert meta["glyph_count"] == len((root / "airlink_fonts.chars.txt").read_text(encoding="utf-8"))

expected_fonts = {
    "medium_17": (17, "Medium", 4, 21, 4, "normal"),
    "medium_18": (18, "Medium", 4, 22, 4, "normal"),
    "medium_20": (20, "Medium", 4, 24, 4, "normal"),
    "medium_21": (21, "Medium", 4, 26, 5, "normal"),
    "medium_27": (27, "Medium", 4, 33, 6, "normal"),
    "small_13": (13, "Medium", 2, 16, 3, "contrast2"),
}
assert set(meta["fonts"]) == set(expected_fonts)
for name, (size, style, bpp, line_height, base_line, render) in expected_fonts.items():
    font = meta["fonts"][name]
    assert (font["size"], font["style"], font["bpp"],
            font["line_height"], font["base_line"], font["render"]) == (
                size, style, bpp, line_height, base_line, render)

assert meta["runtime_width_px"] == {
    "无线模式_small_13": 52,
    "100%_small_13": 34,
    "USB共享已就绪_medium_18": 127,
    "192.168.31.80_small_13": 79,
}
assert set(meta["layout_width_px"]) == set(meta["layout_limit_px"])
overflow = {
    name: (width, meta["layout_limit_px"][name])
    for name, width in meta["layout_width_px"].items()
    if width > meta["layout_limit_px"][name]
}
assert not overflow, overflow
assert meta["layout_width_px"]["status_100_percent"] <= 36
assert meta["layout_width_px"]["status_2_4g"] <= 38
assert meta["layout_width_px"]["status_wired"] <= 38
assert meta["layout_width_px"]["ch347_warning"] <= 160
assert meta["layout_width_px"]["battery_not_ready"] <= 120

font_h = (root / "airlink_fonts.h").read_text(encoding="utf-8")
font_c = (root / "airlink_fonts.c").read_text(encoding="utf-8")
generator = (root / "generate_fonts.py").read_text(encoding="utf-8")
for name in expected_fonts:
    assert f"airlink_font_{name}" in font_h
    assert f"airlink_font_{name}" in font_c
for obsolete in ("regular_10", "regular_11"):
    assert obsolete not in font_h
    assert obsolete not in font_c
assert font_c.count("out->bpp=1") == 0
assert font_c.count("out->bpp=2") == 1
assert font_c.count("out->bpp=4") == 5
assert "out->adv_w=(uint16_t)((g->adv+8U)>>4)" in font_c
assert "out->adv_w=g->adv" not in font_c
assert "per_byte = 8 // bpp" in generator
assert "value |= values[offset + index] << (8 - bpp * (index + 1))" in generator
assert 'render == "contrast2"' in generator

ui = (root / "airlink_ui.c").read_text(encoding="utf-8")
build = (root / "build.sh").read_text(encoding="utf-8")
ipc = (root / "ipc_smoke.c").read_text(encoding="utf-8")
for marker in (
    "PAGE_ANIM_MS 240U", "USB共享已就绪",
    "HDMI · 网口 · 耳机口可用", "HDMI·网口·耳机口", "已打开",
    "连接配网热点", "按住1秒确认",
):
    assert marker in ui

assert "airlink_font_regular_10" not in ui
assert "airlink_font_regular_11" not in ui
assert ui.count("&airlink_font_small_13") >= 20
assert "&airlink_font_micro_11" not in ui
assert "&airlink_font_small_12" not in ui
assert "airlink_font_font_ab_" not in ui
assert '0xc9ddefU, 20, 0, 48, LV_TEXT_ALIGN_LEFT' in ui
assert 'C_MUTED, 75, 111, 90, LV_TEXT_ALIGN_CENTER' in ui

assert "lv_obj_t *battery = plain(status_root, 82, 3, 29, 10);" in ui
assert "static lv_obj_t *status_battery_cells[4]" in ui
assert "plain(battery, (lv_coord_t)(2U + index * 6U), 2, 4, 6)" in ui
assert "lv_obj_t *battery = plain(pages[3], 51, 78, 132, 62);" in ui
assert "static lv_obj_t *battery_cells[4]" in ui
assert "battery_kicker" not in ui
assert "plain(battery, (lv_coord_t)(8U + index * 29U), 10, 23, 42)" in ui
assert "0xd9f4ffU, 0, 3, 76, LV_TEXT_ALIGN_CENTER" in ui
assert 'label_make(chip, "CH347", &airlink_font_small_13, C_CYAN,' in ui
assert "0, 13, 56, LV_TEXT_ALIGN_CENTER" in ui
assert 'label_make(shell, "HUB", &airlink_font_small_13,' in ui
assert "C_CYAN, 0, 6, 48, LV_TEXT_ALIGN_CENTER" in ui
assert 'label_make(wired_group, "HDMI·网口·耳机口", &airlink_font_medium_17,' in ui
assert 'label_make(wired_group, "已打开", &airlink_font_medium_17,' in ui
assert '0xb2c9dcU, 44, 29, 24, LV_TEXT_ALIGN_CENTER' in ui
assert 'C_MUTED, 74, 34, 120, LV_TEXT_ALIGN_CENTER' in ui
assert 'lv_label_set_text(home_subtitle, "HDMI · 网口 · 耳机口可用");' in ui
assert "USB HUB模式" not in ui
assert "wired_dot" not in ui
assert "LVGL alignment=battery-bars+mode-badge+CH347+HUB wired-feature=HEADPHONE" in ipc
assert "LVGL alignment=battery-bars+mode-badge+CH347+HUB wired-feature=HEADPHONE" in build
assert "LVGL wired-layout=HOME_HEADPHONE+PAGE_FEATURE_GROUP usb-hub-mode-label=REMOVED" in ipc
assert "LVGL wired-layout=HOME_HEADPHONE+PAGE_FEATURE_GROUP usb-hub-mode-label=REMOVED" in build
assert meta["layout_width_px"]["home_wired_features"] <= 164
assert meta["layout_width_px"]["wired_features_primary"] <= 190
assert meta["layout_width_px"]["wired_features_open"] <= 190

# R27.1 mode changes stay on the home page. The 64 px arc is non-blocking,
# stops after Linux reaches the early ready phase, and has a hard 3 s cap.
for forbidden in (
    "OVERLAY_TRANSITION", "transition_overlay", "transition_title",
    "transition_note", "模式切换超时", "Linux系统未在8秒内响应",
):
    assert forbidden not in ui
assert "home_transition_arc = arc_make(pages[0], 88, 79, 64, C_CYAN, 2, 0, 72);" in ui
assert "static uint32_t wireless_share_ready(void)" in ui
assert "AIRLINK_UI_STATUS_WIFI_CONNECTED |" in ui
assert "AIRLINK_UI_STATUS_VIRTUALHERE_RUNNING" in ui
assert "return !wireless_share_ready();" in ui
assert "if (home_spinner_should_run() &&" in ui
assert "home_spinner_sync();" in ui
assert "return ui_stats.saver || page_slide_active ||" in ui
assert "page_slide_frame_pending || home_spinner_should_run() ||" in ui
assert "LVGL wireless-spinner=UNTIL_WIFI_AND_VH_READY wired=IMMEDIATE_READY" in ipc
assert "LVGL wireless-spinner=UNTIL_WIFI_AND_VH_READY wired=IMMEDIATE_READY" in build
assert "if (current.wired)\n        return 0U;" in ui
assert "if (mode_transition_wired)\n        return 1U;" in ui
assert "#define SCREEN_SAVER_MS 30000U" in ui
assert "#define SCREEN_SAVER_MS 60000U" not in ui
assert "last_activity" not in ui
assert "static uint64_t saver_deadline;" in ui
assert "saver_deadline = now + (uint64_t)SCREEN_SAVER_MS * TICKS_PER_MS;" in ui
assert "deadline_reached(now, saver_deadline)" in ui
assert "elapsed_ms(saver_deadline" not in ui
assert "provision_success_session" in ui
assert "session != provision_success_session" in ui
assert "AIRLINK_PROVISION_FLAG_SUCCESS" in ui
provision_start = ui.index("static void provision_update")
provision_end = ui.index("static uint32_t mode_controls_locked", provision_start)
provision_update = ui[provision_start:provision_end]
assert "ui_read_time()" not in provision_update
assert "saver_reset(now, AIRLINK_UI_SAVER_RESET_PROVISION_SUCCESS, 1U);" in provision_update
mode_transition = ui[ui.index("void airlink_ui_show_mode_transition"):
                     ui.index("void airlink_ui_set_mode_result")]
assert "saver_reset(now, AIRLINK_UI_SAVER_RESET_GPIOA29, 1U);" in mode_transition
assert "AIRLINK_UI_SAVER_RESET_TOUCH" in ui
assert "LVGL saver-deadline=30000ms clock=RDTIME reset=TOUCH+GPIOA29+PROVISION_SUCCESS underflow=FIXED" in ipc
assert "LVGL saver-deadline=30000ms clock=RDTIME reset=TOUCH+GPIOA29+PROVISION_SUCCESS underflow=FIXED" in build
assert "LVGL saver-reset reason=" in ipc
assert "deadline_ms=30000" in ipc
assert "elapsed_ms(mode_transition_anim_time, now) >= LOCAL_ANIM_MS" in ui
assert "elapsed_ms(mode_transition_started, now) >= 3000U" in ui
assert "mode_transition_active && valid &&" in ui
assert "AIRLINK_SYSTEM_WIRED_READY" not in ui
assert "AIRLINK_SYSTEM_WIRELESS_WAIT_LINK" in ui
assert "AIRLINK_SYSTEM_WIRELESS_PROVISIONING" in ui
assert "return mode_transition_active || mode_transition_slow;" in ui
assert "Linux系统响应较慢" not in ui
assert "仍可滑动查看状态" not in ui
assert 'lv_label_set_text(home_title, "系统启动中");' not in ui
slow_start = ui.index("\n    } else if (mode_transition_slow) {")
slow_end = ui.index("\n    } else if (!valid) {", slow_start)
slow = ui[slow_start:slow_end]
for marker in (
    'if (!valid)',
    'lv_label_set_text(home_title, "系统正在启动");',
    'lv_label_set_text(home_subtitle, "正在等待Linux服务");',
    'lv_label_set_text(home_title, "正在启动无线服务");',
    'lv_label_set_text(home_subtitle, "正在准备网络");',
    'lv_color_hex(C_CYAN)',
):
    assert marker in slow
for forbidden in ("current.wired", "正在切换有线模式", "正在停止无线服务", "C_AMBER"):
    assert forbidden not in slow
fault_pos = ui.index("    if (fault) {")
wired_ready_pos = ui.index("    } else if (current.wired) {", fault_pos)
active_pos = ui.index("    } else if (mode_transition_active) {", wired_ready_pos)
slow_pos = ui.index("    } else if (mode_transition_slow) {", active_pos)
assert fault_pos < wired_ready_pos < active_pos < slow_pos
assert 'lv_label_set_text(home_title, "USB HUB已就绪");' in ui[wired_ready_pos:active_pos]
assert 'lv_label_set_text(home_subtitle, "HDMI · 网口 · 耳机口可用");' in ui[wired_ready_pos:active_pos]
active = ui[active_pos:slow_pos]
assert 'if (!valid)' in active
assert 'lv_label_set_text(home_title, "系统正在启动");' in active
assert 'lv_label_set_text(home_subtitle, "正在等待Linux服务");' in active
assert "current.wired" not in active
assert "正在切换有线模式" not in active
assert "正在停止无线服务" not in active
assert "return mode_transition_active || mode_transition_slow;" in ui
assert '"系统切换中"' not in ui
assert 'ch347_locked ? "切换中" : "切换模式"' in ui
assert 'system_locked ? "暂不可用" : "重新配置"' in ui
assert "!ch347_controls_locked()" in ui
assert "!mode_controls_locked()" in ui

# R27.2.3: the provisioning overlay is hotspot-information only.
for forbidden in (
    "provision_qr", "provision_detail_mode", "lv_qrcode_create",
    "lv_qrcode_update", "扫码连接热点", "手动连接热点",
):
    assert forbidden not in ui
for forbidden in (
    "lv_canvas.c", "lv_qrcode.c", "qrcodegen.c",
):
    assert forbidden not in build
config = (root / "lv_conf.h").read_text(encoding="utf-8")
assert "#define LV_USE_CANVAS 0" in config
assert "#define LV_USE_QRCODE 0" in config
for marker in (
    "static uint32_t provision_dismissed_session;",
    "static uint32_t provision_dismiss_pending;",
    "static void provision_mark_dismissed(void)",
    'lv_label_set_text(provision_state, "连接配网热点");',
    'pointer = append(pointer, "热点名称 ");',
    'lv_label_set_text(provision_line3, "地址 192.168.4.1");',
    'lv_label_set_text(provision_line4, "点击屏幕返回");',
    "provision_dismissed_session != session",
    "failed_edge && ap_ready",
    "failed_edge || new_session",
):
    assert marker in ui, marker
assert 'label_make(provision_overlay, "<"' not in ui
assert 'C_MUTED, 55, 28, 130, LV_TEXT_ALIGN_CENTER' in ui
assert '25, 61, 190, LV_TEXT_ALIGN_CENTER' in ui
assert '25, 103, 190, LV_TEXT_ALIGN_CENTER' in ui
assert '25, 130, 190, LV_TEXT_ALIGN_CENTER' in ui
assert '25, 157, 190, LV_TEXT_ALIGN_CENTER' in ui
assert '40, 190, 160, LV_TEXT_ALIGN_CENTER' in ui
touch_start = ui.index("} else if (overlay == OVERLAY_PROVISION) {")
touch_end = ui.index("} else if (overlay == OVERLAY_NONE", touch_start)
touch = ui[touch_start:touch_end]
assert "provision_mark_dismissed();" in touch
assert "close_overlay();" in touch
assert "go_page(2U, 0U);" in touch
assert "AIRLINK_UI_EVENT_PROVISION_CANCEL" not in touch
assert "provision_update();" not in touch
assert "LVGL provision-view=HOTSPOT_ONLY qr=REMOVED tap=RETURN_WIFI hotspot=KEEP_RUNNING" in ipc
assert "WIFI provisioning=HOTSPOT+CAPTIVE_PORTAL ipc=ABI4" in ipc

# R27.2.9: selected B profile is the only small UI font.
for forbidden in (
    "OVERLAY_FONT_TEST", "FONT_TEST_REOPEN_MS", "font_test_overlay",
    "font_test_strip", "font_test_pages", "show_font_test",
    "AIRLINK_UI_EVENT_FONT_PROFILE", "FONT TEST profile=",
    "A · Noto 13px", "B · Noto 13px", "C · Fusion 12px",
):
    assert forbidden not in ui
assert "LVGL font-small=NOTO13_M2_CONTRAST enhanced=ON wifi-row=CENTERED" in ipc
assert "LVGL font-test=" not in ipc
assert "page_index == 3U && !hold_sent" not in ui

# R27.2: a physical switch to wired mode must synchronously evict the
# provisioning overlay and stale mandatory/AP_READY state must never reopen it.
for marker in (
    "static void provision_view_reset(void)",
    "static uint32_t provision_overlay_should_close(void)",
    "static void provision_pending_clear(void)",
    "return current.wired ||",
    "(!active && current.provision.phase == AIRLINK_PROVISION_IDLE)",
    "current.provision.phase == AIRLINK_PROVISION_SUCCESS",
    "if (current.wired)\n        return;",
    "if (active && ap_ready && !mode_controls_locked()",
    "if (current.wired || mode_controls_locked())",
):
    assert marker in ui
tick_start = ui.index("int airlink_ui_tick")
refresh_start = ui.index("current = *model;", tick_start)
refresh = ui[refresh_start:
             ui.index("last_model_refresh = now;", refresh_start)]
assert refresh.index("provision_overlay_should_close()") < refresh.index("page_update_or_defer(now);")
for marker in (
    "provision_pending_clear();", "provision_view_reset();",
    "close_overlay();", "go_page(0U, 0U);",
):
    assert marker in refresh
mode_change = ui[ui.index("void airlink_ui_show_mode_transition"):
                 ui.index("void airlink_ui_set_mode_result")]
assert "if (wired)" in mode_change
assert "provision_pending_clear();" in mode_change
assert "provision_view_reset();" in mode_change
assert mode_change.index("provision_view_reset();") < mode_change.index("close_overlay();")
assert "AIRLINK_UI_EVENT_PROVISION_REQUEST" in ui
assert "AIRLINK_UI_EVENT_PROVISION_CANCEL" in ui

# A successful STA test already means the profile was committed and IPv4 is
# available. The provisioning overlay must return to the wireless home page
# immediately instead of pretending that VirtualHere startup is blocking it.
assert "static uint32_t provision_success_seen;" not in ui
assert 'lv_label_set_text(provision_line2, "VirtualHere正在启动");' not in ui
assert "overlay == OVERLAY_PROVISION &&" in ui
assert "current.provision.phase == AIRLINK_PROVISION_SUCCESS" in ui
success_close = ui.index("overlay == OVERLAY_PROVISION &&")
assert "close_overlay();" in ui[success_close:success_close + 300]
assert "go_page(0U, 0U);" in ui[success_close:success_close + 300]

assert "#define C_SCREEN 0x000000U" in ui
assert "lv_obj_set_style_bg_color(screen, lv_color_black(), 0)" in ui
assert '#include "ui_assets.h"' not in ui
assert "airlink_background_image" not in ui
assert "lv_img_create" not in ui
assert " ui_assets)" not in build
for obsolete in (
    "ui_assets.c", "ui_assets.h", "generate_ui_assets.py",
    "airlink_background_preview.png", "r20-ui-preview.png", "r23-ui-preview.png", "r25-ui-preview.png",
):
    assert not (root / obsolete).exists(), obsolete

wifi_icon = re.search(
    r"static void create_wifi_status_icon\(void\)\n\{(.*?)\n\}",
    ui, re.S,
).group(1)
assert "plain(status_root, 0, 0, 15, 14)" in wifi_icon
assert wifi_icon.count("line_make(") == 2
assert "arc_make(" not in wifi_icon
assert "wifi_outer_points, 7" in wifi_icon
assert "wifi_inner_points, 5" in wifi_icon
assert 'status_mode = label_make(status_root, "--", &airlink_font_small_13' in ui
assert 'if (!connected) lv_label_set_text(status_mode, "--");' in ui
assert 'lv_label_set_text(status_mode, "5G");' in ui
assert 'lv_label_set_text(status_mode, "2.4G");' in ui

wifi_row = re.search(
    r"static void wifi_state_set_text\(const char \*text\)\n\{(.*?)\n\}",
    ui, re.S,
).group(1)
for statement in (
    "lv_label_set_text(wifi_state, text);",
    "lv_obj_update_layout(wifi_state);",
    "text_width = lv_obj_get_width(wifi_state);",
    "total_width = 5 + 4 + text_width;",
    "start = total_width < 240 ? (240 - total_width) / 2 : 0;",
    "lv_obj_set_pos(wifi_state_dot, start, 141);",
    "lv_obj_set_pos(wifi_state, start + 9, 136);",
):
    assert statement in wifi_row
assert ui.count("lv_label_set_text(wifi_state,") == 1
for state in ("等待网络状态", "Wi-Fi未连接"):
    assert f'wifi_state_set_text("{state}")' in ui
assert 'wifi_state_set_text(line);' in ui

assert "static lv_obj_t *saver_dot;" in ui
assert ui.count("saver_dot = plain(") == 1
assert "saver_orbit_phase = (saver_orbit_phase + 1U) % 120U;" in ui
assert "elapsed_ms(saver_orbit_time, now) < SAVER_ANIM_MS" in ui
assert "lv_trigo_sin((int16_t)(angle + 90))" in ui
assert "lv_trigo_sin(angle)" in ui
for forbidden in ("saver_rings", "saver_dots"):
    assert forbidden not in ui

assert "static lv_obj_t *saver_usb;" in ui
assert "static lv_obj_t *saver_usb_pulse;" in ui
assert "saver_usb_stem_points" in ui
assert "saver_usb_arrow_points" in ui
assert "saver_usb_branch_points" in ui
assert ui.count("line_make(saver_usb,") == 3
assert "saver_usb_phase = (saver_usb_phase + 1U) % 36U;" in ui
assert "elapsed_ms(saver_usb_time, now) < SAVER_ANIM_MS" in ui
assert "wave = lv_trigo_sin" in ui
assert "lv_obj_set_style_opa(saver_usb_pulse" in ui

saver_mode = re.search(
    r"static void saver_update_mode\(void\)\n\{(.*?)\n\}",
    ui, re.S,
).group(1)
for statement in (
    "lv_obj_add_flag(saver_dot, LV_OBJ_FLAG_HIDDEN);",
    "lv_obj_add_flag(saver_word, LV_OBJ_FLAG_HIDDEN);",
    "lv_obj_clear_flag(saver_usb, LV_OBJ_FLAG_HIDDEN);",
    "lv_obj_clear_flag(saver_dot, LV_OBJ_FLAG_HIDDEN);",
    "lv_obj_clear_flag(saver_word, LV_OBJ_FLAG_HIDDEN);",
    "lv_obj_add_flag(saver_usb, LV_OBJ_FLAG_HIDDEN);",
):
    assert statement in saver_mode
assert "static void saver_reset_animation(uint64_t now)" in ui
assert ui.count("saver_reset_animation(now);") == 2
assert ui.count("    animate_saver(now);") == 1

display = (root / "display.c").read_text(encoding="utf-8")
header = (root / "display.h").read_text(encoding="utf-8")
for legacy in (
    "show_battery_scene", "draw_seven_segment_digit",
    "airlink_display_show_battery", "airlink_display_show_ch347",
    "airlink_display_draw_touch", "airlink_display_next_frame",
):
    assert legacy not in display
    assert legacy not in header
    assert legacy not in ipc
assert "LVGL owns every visible pixel after airlink_ui_init()" in ipc
visual_marker = (
    "LVGL visual-polish=PURE_BLACK wifi=POLYLINE "
    "saver=WIRELESS_DOT+WIRED_USB outer-ring=REMOVED"
)
font_marker = "LVGL font-small=NOTO13_M2_CONTRAST enhanced=ON wifi-row=CENTERED"
assert visual_marker in ipc and visual_marker in build
assert font_marker in ipc and font_marker in build
provision_exit_marker = (
    "LVGL provision-mode-exit=WIRED_FORCE_CLOSE stale-state-guard=ON"
)
assert provision_exit_marker in ipc and provision_exit_marker in build
assert "background=PURE_BLACK" in ipc
assert "background=CIRCULAR-GRADIENT" not in ipc
assert "airlink_display_flush_rgb565" in display
assert display.count("panel_fill(status,") == 1
assert display.count("panel_fill_rect(status,") == 1

# R27.5: actual-mode pinout marks the screen side and CH347 is locally controlled.
assert 'ch347_pin_button = plain(pages[1], 35, 163, 80, 30);' in ui
assert 'label_make(ch347_pin_button, "引脚对照",' in ui
assert "OVERLAY_PINOUT" in ui
assert "pinout_update(current.ch347_current);" in ui
assert "pinout_view_mode" not in ui
assert "pinout_previous" not in ui
assert "pinout_next" not in ui
assert meta["layout_width_px"]["pinout_cell"] <= 35
assert meta["layout_width_px"]["pinout_screen_direction"] <= 84
assert 'label_make(screen_direction, "屏幕方向"' in ui
assert "screen_neck" not in ui and "screen_foot" not in ui
pinout_marker = (
    "LVGL ch347-pinout=PIN_ONLY orientation=CW90 layout=2x6 "
    "rows=SCREEN_SIDE_TOP colors=5V_RED+GND_GRAY+SIGNAL_BLUE "
    "screen-direction=ROUNDED_LABEL tap=RETURN"
)
local_marker = "LVGL ch347-control=LOCAL_ALWAYS system-lock=REMOVED"
assert pinout_marker in ipc and pinout_marker in build
assert local_marker in ipc and local_marker in build

print("R27.6.6.23 local CH347 + rounded screen-direction label tests: PASS")
