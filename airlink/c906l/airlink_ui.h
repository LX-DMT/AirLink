#ifndef AIRLINK_UI_H
#define AIRLINK_UI_H
#include <stdint.h>
#include "airlink_ipc_v4.h"
#include "ch347.h"
#include "display.h"
#include "touch.h"

#define AIRLINK_UI_EVENT_NONE 0
#define AIRLINK_UI_EVENT_CH347_REQUEST 1
#define AIRLINK_UI_EVENT_PROVISION_REQUEST 2
#define AIRLINK_UI_EVENT_PROVISION_CANCEL 3

#define AIRLINK_UI_SAVER_RESET_NONE 0U
#define AIRLINK_UI_SAVER_RESET_TOUCH 1U
#define AIRLINK_UI_SAVER_RESET_GPIOA29 2U
#define AIRLINK_UI_SAVER_RESET_PROVISION_SUCCESS 3U

#define AIRLINK_UI_CH347_IDLE 0U
#define AIRLINK_UI_CH347_PREPARING 1U
#define AIRLINK_UI_CH347_SWITCHING 2U
#define AIRLINK_UI_CH347_ENUMERATING 3U
#define AIRLINK_UI_CH347_SUCCESS 4U
#define AIRLINK_UI_CH347_ERROR 5U

struct airlink_ui_model {
    uint32_t wired;
    uint32_t battery_mv;
    uint32_t battery_percent;
    uint32_t battery_valid;
    uint32_t ch347_current;
    uint32_t ch347_selected;
    uint32_t ch347_state;
    struct airlink_ipc_ui_status network;
    struct airlink_ipc_provision_status provision;
};

struct airlink_ui_stats {
    uint32_t ready;
    uint32_t page;
    uint32_t saver;
    uint32_t transition_count;
    uint32_t flush_count;
    uint32_t flush_bytes;
    uint32_t flush_avg_us;
    uint32_t flush_max_us;
    uint32_t fps_current;
    uint32_t fps_min;
    uint32_t frame_count;
    uint32_t frame_bytes;
    uint32_t full_frame_count;
    uint32_t partial_frame_count;
    uint32_t page_frame_count;
    uint32_t page_frame_avg_us;
    uint32_t page_frame_max_us;
    uint32_t missed_refresh;
    uint32_t spi_parent_hz;
    uint32_t spi_sclk_hz;
    uint32_t heap_free;
    uint32_t heap_used_pct;
    uint32_t loop_max_us;
    uint32_t first_frame_ms;
    uint32_t visible_ms;
};

int airlink_ui_init(struct airlink_display_status *display,
                    struct airlink_touch_status *touch,
                    const struct airlink_ui_model *model,
                    uint64_t now);
int airlink_ui_tick(const struct airlink_ui_model *model,
                    const struct airlink_touch_status *touch,
                    uint64_t now, uint32_t *event_arg);
void airlink_ui_set_ch347_result(uint32_t state);
void airlink_ui_show_mode_transition(uint32_t wired);
void airlink_ui_set_mode_result(uint32_t success, uint32_t wired,
                                uint32_t phase, uint32_t error);
void airlink_ui_note_loop_us(uint32_t loop_us);
void airlink_ui_get_stats(struct airlink_ui_stats *stats);
uint32_t airlink_ui_take_saver_reset_reason(void);
#endif
