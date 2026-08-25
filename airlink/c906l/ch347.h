#ifndef AIRLINK_CH347_H
#define AIRLINK_CH347_H

#include <stdint.h>

enum airlink_ch347_mode {
    AIRLINK_CH347_DUAL_UART = 0,
    AIRLINK_CH347_SPI_I2C = 1,
    AIRLINK_CH347_SPI_I2C_DRIVERLESS = 2,
    AIRLINK_CH347_JTAG_UART = 3,
    AIRLINK_CH347_MODE_COUNT = 4,
};

#define AIRLINK_CH347_ERROR_NONE              0U
#define AIRLINK_CH347_ERROR_INVALID_MODE      (1U << 0)
#define AIRLINK_CH347_ERROR_PINMUX            (1U << 1)
#define AIRLINK_CH347_ERROR_DIRECTION         (1U << 2)
#define AIRLINK_CH347_ERROR_OUTPUT_READBACK   (1U << 3)

#define AIRLINK_CH347_OWNER_PINMUX_RESET      (1U << 0)
#define AIRLINK_CH347_OWNER_PINMUX_DTR        (1U << 1)
#define AIRLINK_CH347_OWNER_PINMUX_RTS        (1U << 2)
#define AIRLINK_CH347_OWNER_DIRECTION         (1U << 3)
#define AIRLINK_CH347_OWNER_RESET_LEVEL       (1U << 4)
#define AIRLINK_CH347_OWNER_MODE_LEVELS       (1U << 5)

struct airlink_ch347_status {
    uint32_t ready;
    uint32_t error_flags;
    uint32_t current_mode;
    uint32_t dtr_level;
    uint32_t rts_level;
    uint32_t reset_level;
    uint32_t switch_count;
    uint32_t recovery_count;
    uint32_t ownership_mismatch;
    uint32_t last_owner_mismatch;
    uint32_t pinmux_reset;
    uint32_t pinmux_dtr;
    uint32_t pinmux_rts;
    uint32_t gpio_out;
    uint32_t gpio_oe;
    uint32_t gpio_ext;
    uint32_t pulse_ms;
    uint32_t pending_mode;
    uint32_t switching;
    uint32_t last_switch_mismatch;
    uint32_t switch_recovery_count;
    uint64_t release_deadline;
};

int airlink_ch347_init(struct airlink_ch347_status *status);
int airlink_ch347_apply_mode(struct airlink_ch347_status *status,
                             uint32_t mode);
int airlink_ch347_begin_mode(struct airlink_ch347_status *status,
                             uint32_t mode, uint64_t now);
int airlink_ch347_tick(struct airlink_ch347_status *status, uint64_t now);
int airlink_ch347_check_ownership(struct airlink_ch347_status *status);
const char *airlink_ch347_mode_name(uint32_t mode);

#endif
