#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parent
display = (root / "display.c").read_text()
display_h = (root / "display.h").read_text()
port = (root / "lv_port.c").read_text()
ui = (root / "airlink_ui.c").read_text()
ui_h = (root / "airlink_ui.h").read_text()
ipc = (root / "ipc_smoke.c").read_text()
conf = (root / "lv_conf.h").read_text()

# 46.875 MHz is the validated GC9A01 clock and gives a 19.6608 ms
# theoretical wire time for one 240x240 RGB565 frame.
assert 1_500_000_000 // 8 // 4 == 46_875_000
frame_bytes = 240 * 240 * 2
wire_us = frame_bytes * 8 * 1_000_000 // 46_875_000
assert 19_600 <= wire_us <= 19_700
for marker in (
    "#define CLOCK_DIV_SAFE          8U",
    "#define SPI_DIVIDER_SAFE        4U",
    "CLOCK_DIV_APPLY",
    "clock_set_divider(status, CLOCK_DIV_SAFE)",
):
    assert marker in display
assert "CLOCK_DIV_FAST" not in display
assert "SPI_DIVIDER_FAST" not in display
assert "status->spi_parent_hz = FPLL_HZ / CLOCK_DIV_SAFE;" in display
assert "status->spi_sclk_hz = status->spi_parent_hz / SPI_DIVIDER_SAFE;" in display
assert "AIRLINK_DISPLAY_ERROR_CLOCK_FALLBACK" in display_h
assert "GC9A01 SPI parent=187500000 sclk=46875000 baudr=4 SAFE" in ipc
assert "SPI75MHZ" not in ipc

# One full-screen draw buffer: one page frame can be emitted in one flush,
# while full_refresh/direct_mode stay disabled for efficient local updates.
assert "buffer_a[240U * 240U]" in port
assert "buffer_b[" not in port
assert "lv_disp_draw_buf_init(&draw_buffer, buffer_a, NULL, 240U * 240U)" in port
assert "display_driver.full_refresh = 0;" in port
assert "display_driver.direct_mode = 0;" in port
assert "lv_disp_flush_is_last(drv)" in port
assert "airlink_display_complete_frame(port_display)" in port
assert "#define LV_DISP_DEF_REFR_PERIOD 16" in conf

for marker in (
    "#define PAGE_ANIM_MS 240U",
    "#define PAGE_FRAME_MS 20U",
    "#define LOCAL_ANIM_MS 16U",
    "#define SAVER_ANIM_MS 33U",
    "page_slide_step(now);",
    "lv_bezier3(t, 0U, 900U, 950U, LV_BEZIER_VAL_MAX)",
    "page_slide_last_step",
    "page_slide_frame_pending",
):
    assert marker in ui
for obsolete in (
    "page_anim_exec",
    "lv_anim_set_var(&animation, pages_strip)",
    "lv_anim_set_path_cb(&animation, lv_anim_path_ease_out)",
    "#define PAGE_ANIM_MS 280U",
):
    assert obsolete not in ui

# The custom curve is monotonic and produces twelve 20 ms positions,
# ending exactly at the target after 240 ms.
def bezier3(t, u0, u1, u2, u3):
    rem = 1024 - t
    rem2 = rem * rem >> 10
    rem3 = rem2 * rem >> 10
    t2 = t * t >> 10
    t3 = t2 * t >> 10
    return (
        (rem3 * u0 >> 10)
        + (3 * rem2 * t * u1 >> 20)
        + (3 * rem * t2 * u2 >> 20)
        + (t3 * u3 >> 10)
    )

positions = []
for elapsed in range(20, 241, 20):
    if elapsed >= 240:
        value = -240
    else:
        step = bezier3(elapsed * 1024 // 240, 0, 900, 950, 1024)
        value = (-240 * step) >> 10
    positions.append(value)
assert len(positions) == 12
assert all(a >= b for a, b in zip(positions, positions[1:]))
assert positions[-1] == -240
assert len(set(positions)) == 12

# Model metadata-only publications are ignored; real visible changes are
# coalesced until the page slide ends.
for marker in (
    "model_visual_changed",
    "Ignore IPC generation/CRC/counters",
    "page_update_or_defer",
    "page_refresh_pending = 1U",
    "page_refresh_pending_since = now",
    "elapsed_ms(page_refresh_pending_since, now) >= PAGE_ANIM_MS",
    "if (page_refresh_pending)",
):
    assert marker in ui
assert "a->update_count" not in ui
assert "pa->elapsed_sec" not in ui

assert "animation_period_ms(uint32_t kind)" in ui
assert "return PAGE_FRAME_MS;" in ui
assert "return SAVER_ANIM_MS;" in ui
assert "return LOCAL_ANIM_MS;" in ui
assert "gap > period * 2U" in ui
assert "performance_sample(finished, frames, kind)" in ui
for field in (
    "fps_current", "fps_min", "frame_count", "flush_avg_us",
    "flush_max_us", "frame_bytes", "full_frame_count",
    "partial_frame_count", "page_frame_count", "page_frame_avg_us",
    "page_frame_max_us", "loop_max_us", "missed_refresh",
    "spi_parent_hz", "spi_sclk_hz",
):
    assert field in ui_h
    assert field in ipc

assert (
    "LVGL refresh-pacing=SAFE46 buffer=1x240x240 "
    "page=20ms/240ms spinner=16ms saver=33ms model=COALESCED"
) in ipc
print("R27.6.6.23 safe 46.875MHz full-buffer refresh pacing tests: PASS")
