#ifndef AIRLINK_DISPLAY_H
#define AIRLINK_DISPLAY_H

#include <stdint.h>

#define AIRLINK_DISPLAY_ERROR_SPI_TIMEOUT    (1U << 0)
#define AIRLINK_DISPLAY_ERROR_OWNER_CHANGED  (1U << 1)
#define AIRLINK_DISPLAY_ERROR_CLOCK_FALLBACK (1U << 2)

struct airlink_display_status {
    uint32_t ready;
    uint32_t error_flags;
    uint32_t init_stage;
    uint32_t frame_count;
    uint32_t last_colour;
    uint32_t spi_diagnostic;
    uint32_t fifo_depth;
    uint32_t max_txflr;
    uint32_t last_frame_cycles_low;
    uint32_t last_frame_cycles_high;
    uint32_t clock_div_register;
    uint32_t clock_bypass_register;
    uint32_t reset_done_ms;
    uint32_t sleep_out_done_ms;
    uint32_t gram_ready_ms;
    uint32_t visible_ms;
    uint32_t ready_ms;
    uint32_t visible;
    uint32_t flush_count;
    uint32_t flush_bytes;
    uint32_t flush_max_cycles;
    uint64_t flush_total_cycles;
    uint32_t completed_frame_count;
    uint32_t current_frame_bytes;
    uint32_t last_frame_bytes;
    uint32_t spi_parent_hz;
    uint32_t spi_sclk_hz;
    uint32_t spi_baudr;
    uint32_t spi_high_speed;
    uint32_t clock_bypass_register_1;
};

int airlink_display_init(struct airlink_display_status *status);
int airlink_display_make_visible(struct airlink_display_status *status);
int airlink_display_flush_rgb565(struct airlink_display_status *status,
                                 uint32_t x1, uint32_t y1,
                                 uint32_t x2, uint32_t y2,
                                 const uint16_t *pixels);
void airlink_display_complete_frame(struct airlink_display_status *status);
void airlink_display_check_ownership(struct airlink_display_status *status);
const char *airlink_display_colour_name(uint32_t colour);

#endif
