#include "lvgl.h"
#include "display.h"
#include "touch.h"

static struct airlink_display_status *port_display;
static struct airlink_touch_status *port_touch;
static lv_disp_draw_buf_t draw_buffer;
/*
 * One full RGB565 frame lets LVGL render a page transition as one flush.
 * A second full-screen buffer would consume another 115.2 KiB without
 * helping this blocking, polling SPI transport, so keep this single-buffered.
 */
static lv_color_t buffer_a[240U * 240U];
static lv_disp_drv_t display_driver;
static lv_indev_drv_t input_driver;
static uint32_t flush_recovery_pending;
static uint32_t frame_flush_failed;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels)
{
    int result = -1;

    if (port_display != 0)
        result = airlink_display_flush_rgb565(port_display,
            (uint32_t)area->x1, (uint32_t)area->y1,
            (uint32_t)area->x2, (uint32_t)area->y2,
            (const uint16_t *)pixels);
    if (result != 0) {
        flush_recovery_pending = 1U;
        frame_flush_failed = 1U;
    }
    if (lv_disp_flush_is_last(drv)) {
        if (result == 0 && frame_flush_failed == 0U)
            airlink_display_complete_frame(port_display);
        frame_flush_failed = 0U;
    }
    lv_disp_flush_ready(drv);
}

static void read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    if (port_touch != 0 && port_touch->ready && port_touch->points != 0U) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = (lv_coord_t)port_touch->x;
        data->point.y = (lv_coord_t)port_touch->y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->continue_reading = false;
}

void airlink_lv_port_init(struct airlink_display_status *display,
                          struct airlink_touch_status *touch)
{
    port_display = display;
    port_touch = touch;
    flush_recovery_pending = 0U;
    frame_flush_failed = 0U;
    lv_disp_draw_buf_init(&draw_buffer, buffer_a, NULL, 240U * 240U);
    lv_disp_drv_init(&display_driver);
    display_driver.hor_res = 240;
    display_driver.ver_res = 240;
    display_driver.flush_cb = flush_cb;
    display_driver.draw_buf = &draw_buffer;
    display_driver.full_refresh = 0;
    display_driver.direct_mode = 0;
    (void)lv_disp_drv_register(&display_driver);
    lv_indev_drv_init(&input_driver);
    input_driver.type = LV_INDEV_TYPE_POINTER;
    input_driver.read_cb = read_cb;
    (void)lv_indev_drv_register(&input_driver);
}

uint32_t airlink_lv_port_take_full_redraw_request(void)
{
    uint32_t pending = flush_recovery_pending;
    flush_recovery_pending = 0U;
    return pending;
}
