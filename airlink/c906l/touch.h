#ifndef AIRLINK_TOUCH_H
#define AIRLINK_TOUCH_H

#include <stdint.h>

#define AIRLINK_TOUCH_ERROR_NONE                0U
#define AIRLINK_TOUCH_ERROR_COMPONENT           1U
#define AIRLINK_TOUCH_ERROR_DISABLE_TIMEOUT     2U
#define AIRLINK_TOUCH_ERROR_ENABLE_TIMEOUT      3U
#define AIRLINK_TOUCH_ERROR_BUS_BUSY            4U
#define AIRLINK_TOUCH_ERROR_TX_TIMEOUT          5U
#define AIRLINK_TOUCH_ERROR_RX_TIMEOUT          6U
#define AIRLINK_TOUCH_ERROR_STOP_TIMEOUT        7U
#define AIRLINK_TOUCH_ERROR_TX_ABORT            8U
#define AIRLINK_TOUCH_ERROR_CONFIG              9U
#define AIRLINK_TOUCH_ERROR_OWNER               10U
#define AIRLINK_TOUCH_ERROR_PLATFORM            11U

#define AIRLINK_TOUCH_SOURCE_NONE       0U
#define AIRLINK_TOUCH_SOURCE_IRQ        1U
#define AIRLINK_TOUCH_SOURCE_POLL       2U

#define AIRLINK_TOUCH_RESULT_NONE               0
#define AIRLINK_TOUCH_RESULT_EVENT              1
#define AIRLINK_TOUCH_RESULT_OWNER_RECOVERED    2

#define AIRLINK_TOUCH_WARNING_OWNER_RECOVERED   (1U << 0)

struct airlink_touch_status {
        uint32_t ready;
        uint32_t error_flags;
        uint32_t warning_flags;
        uint32_t init_stage;
        uint32_t event_count;
        uint32_t error_count;
        uint32_t warning_count;
        uint32_t recovery_count;
        uint32_t reinit_count;
        uint32_t owner_recovery_count;
        uint32_t poll_count;
        uint32_t successful_poll_count;
        uint32_t irq_level;
        uint32_t chip_id;
        uint32_t firmware_version;
        uint32_t irq_control;
        uint32_t auto_sleep_control;
        uint32_t gesture;
        uint32_t points;
        uint32_t event;
        uint32_t raw_x;
        uint32_t raw_y;
        uint32_t x;
        uint32_t y;
        uint32_t i2c_abort_source;
        uint32_t i2c_status;
        uint32_t component_type;
        uint32_t last_error;
        uint32_t last_register;
        uint32_t source;
        uint32_t owner_changed;
        uint32_t last_owner_mismatch;
        uint32_t clock_enable_1;
        uint32_t clock_enable_3;
        uint32_t reset_state;
        uint32_t clock_divider;
        uint32_t clock_bypass;
        uint32_t ownership_mismatch;
        uint32_t pinmux_scl;
        uint32_t pinmux_sda;
        uint32_t pinmux_irq;
        uint32_t pinmux_reset;
        uint32_t gpio_data;
        uint32_t gpio_direction;
        uint32_t gpio_ext_port;
        uint8_t raw[7];
};

int airlink_touch_init(struct airlink_touch_status *status);
int airlink_touch_poll(struct airlink_touch_status *status);
void airlink_touch_check_ownership(struct airlink_touch_status *status);
const char *airlink_touch_error_name(uint32_t error);
const char *airlink_touch_source_name(uint32_t source);
const char *airlink_touch_event_name(uint32_t event, uint32_t points);

#endif
