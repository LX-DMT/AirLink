#!/usr/bin/env python3
import json
from pathlib import Path

root = Path(__file__).resolve().parent
ui = (root / "airlink_ui.c").read_text(encoding="utf-8")
build = (root / "build.sh").read_text(encoding="utf-8")
ipc = (root / "ipc_smoke.c").read_text(encoding="utf-8")
meta = json.loads((root / "airlink_fonts.json").read_text(encoding="utf-8"))

for forbidden in (
    "pinout_mode_title", "pinout_mode_number", "pinout_view_mode",
    "pinout_previous", "pinout_next", "pinout_mode_titles",
    "pinout_themes", "PINOUT_ACTIVITY",
):
    assert forbidden not in ui, forbidden

create = ui[ui.index("pinout_overlay = plain"):
            ui.index("result_overlay = plain",
                     ui.index("pinout_overlay = plain"))]
for marker in (
    "plain(pinout_overlay, 78, 48, 84, 30)",
    "panel_style(screen_direction, C_PANEL, LV_OPA_20",
    'label_make(screen_direction, "屏幕方向"',
    "0, 7, 84, LV_TEXT_ALIGN_CENTER",
    "for (uint32_t row = 0U; row < 2U; ++row)",
    "for (uint32_t column = 0U; column < 6U; ++column)",
    "uint32_t index = row * 6U + column;",
    "lv_coord_t x = 7 + (lv_coord_t)column * 38;",
    "lv_coord_t y = row == 0U ? 94 : 130;",
    "plain(pinout_overlay, x, y, 35, 28)",
    "C_BLUE, 0, 6, 35",
):
    assert marker in create, marker
for forbidden in ('label_make(pinout_overlay, "<"', '"引脚对照"',
                  '"MODE0', '"1 / 4"', '">"', "screen_neck",
                  "screen_foot", "screen_icon"):
    assert forbidden not in create, forbidden
assert '"屏幕方向"' in create

expected_rows = (
    '"RI0", "DCD0", "GND", "GND", "5V", "5V",',
    '"RXD0", "TXD0", "RTS0", "CTS0", "DSR0", "DTR0"',
    '"SCL", "ACT", "GND", "GND", "5V", "5V",',
    '"SDA", "MOSI", "MISO", "SCK", "SCS0", "SCS1"',
    '"NC", "ACT", "GND", "GND", "5V", "5V",',
    '"NC", "TDI", "TDO", "TCK", "TMS", "TRST"',
)
for row in expected_rows:
    assert row in ui, row
assert ui.count(expected_rows[2]) == 2
assert ui.count(expected_rows[3]) == 2

colour = ui[ui.index("static uint32_t pinout_colour"):
            ui.index("static void ipv4_text")]
assert "PINOUT_POWER:\n        return C_ERROR;" in colour
assert "PINOUT_GROUND:\n        return C_PIN_GROUND;" in colour
assert "PINOUT_NC:\n        return C_DIM;" in colour
assert "return C_BLUE;" in colour
assert "PINOUT_ACTIVITY" not in colour

show = ui[ui.index("static void show_pinout(void)"):
          ui.index("static void mode_picker_update")]
assert "pinout_update(current.ch347_current);" in show
for forbidden in ("ch347_selected", "pending_event", "AIRLINK_UI_EVENT_",
                  "DTR1", "RTS1", "RST"):
    assert forbidden not in show, forbidden

touch_start = ui.index("} else if (overlay == OVERLAY_PINOUT) {")
touch_end = ui.index("} else if (overlay == OVERLAY_PROVISION) {",
                     touch_start)
touch = ui[touch_start:touch_end]
assert "if (ax < 18 && ay < 18)" in touch
assert "close_overlay();" in touch
for forbidden in ("pinout_next", "pinout_previous", "SWIPE_MIN",
                  "pending_event", "ch347_selected", "AIRLINK_UI_EVENT_"):
    assert forbidden not in touch, forbidden

assert meta["layout_width_px"]["pinout_cell"] <= 35
assert meta["layout_width_px"]["pinout_screen_direction"] <= 84
for removed in ("pinout_title", "pinout_mode0", "pinout_mode1",
                "pinout_mode2", "pinout_mode3"):
    assert removed not in meta["layout_width_px"]

serial = (
    "LVGL ch347-pinout=PIN_ONLY orientation=CW90 layout=2x6 "
    "rows=SCREEN_SIDE_TOP colors=5V_RED+GND_GRAY+SIGNAL_BLUE "
    "screen-direction=ROUNDED_LABEL tap=RETURN"
)
assert serial in ipc
assert serial in build
assert ui.count("lv_obj_add_flag(pinout_overlay, LV_OBJ_FLAG_HIDDEN);") >= 2
print("R27.6.6.23 CH347 rounded screen-direction label tests: PASS")
