#include <stdint.h>

#include "touch.h"

#define I2C4_BASE                       0x04040000UL
#define IC_CON                          (I2C4_BASE + 0x00)
#define IC_TAR                          (I2C4_BASE + 0x04)
#define IC_DATA_CMD                     (I2C4_BASE + 0x10)
#define IC_SS_SCL_HCNT                  (I2C4_BASE + 0x14)
#define IC_SS_SCL_LCNT                  (I2C4_BASE + 0x18)
#define IC_INTR_MASK                    (I2C4_BASE + 0x30)
#define IC_RAW_INTR_STAT                (I2C4_BASE + 0x34)
#define IC_RX_TL                        (I2C4_BASE + 0x38)
#define IC_TX_TL                        (I2C4_BASE + 0x3c)
#define IC_CLR_INTR                     (I2C4_BASE + 0x40)
#define IC_CLR_TX_ABRT                  (I2C4_BASE + 0x54)
#define IC_CLR_STOP_DET                 (I2C4_BASE + 0x60)
#define IC_ENABLE                       (I2C4_BASE + 0x6c)
#define IC_STATUS                       (I2C4_BASE + 0x70)
#define IC_RXFLR                        (I2C4_BASE + 0x78)
#define IC_TX_ABRT_SOURCE               (I2C4_BASE + 0x80)
#define IC_ENABLE_STATUS                (I2C4_BASE + 0x9c)
#define IC_COMP_TYPE                    (I2C4_BASE + 0xfc)

#define IC_CON_MASTER                   (1U << 0)
#define IC_CON_SPEED_STANDARD           (1U << 1)
#define IC_CON_RESTART_ENABLE           (1U << 5)
#define IC_CON_SLAVE_DISABLE            (1U << 6)
#define IC_CON_R14_VALUE                (IC_CON_MASTER | IC_CON_SPEED_STANDARD | \
                                         IC_CON_RESTART_ENABLE | IC_CON_SLAVE_DISABLE)

#define IC_DATA_CMD_READ                (1U << 8)
#define IC_DATA_CMD_STOP                (1U << 9)
#define IC_DATA_CMD_RESTART             (1U << 10)
#define IC_INTR_TX_ABRT                 (1U << 6)
#define IC_INTR_STOP_DET                (1U << 9)
#define IC_STATUS_ACTIVITY              (1U << 0)
#define IC_STATUS_TFNF                  (1U << 1)
#define IC_STATUS_TFE                   (1U << 2)
#define IC_STATUS_RFNE                  (1U << 3)
#define IC_STATUS_MASTER_ACTIVITY       (1U << 5)

#define I2C_COMPONENT_TYPE              0x44570140U
#define I2C_TARGET_ADDRESS              0x15U
#define I2C_SS_HCNT_100MHZ              393U
#define I2C_SS_LCNT_100MHZ              469U

#define CLOCK_BASE                      0x03002000UL
#define CLOCK_ENABLE_1                  (CLOCK_BASE + 0x004)
#define CLOCK_ENABLE_3                  (CLOCK_BASE + 0x00c)
#define CLOCK_BYPASS_0                  (CLOCK_BASE + 0x030)
#define CLOCK_I2C_DIV                   (CLOCK_BASE + 0x104)
#define CLOCK_APB_I2C_ENABLE            (1U << 6)
#define CLOCK_I2C_ENABLE                (1U << 7)
#define CLOCK_APB_I2C4_ENABLE           (1U << 21)
#define RESET_BASE                      0x03003000UL
#define RESET_CONTROL                   (RESET_BASE + 0x000)
#define RESET_I2C4_DEASSERT             (1U << 31)

#define PINMUX_I2C4_SCL                 0x03001090UL
#define PINMUX_I2C4_SDA                 0x03001098UL
#define PINMUX_TOUCH_IRQ                0x03001084UL
#define PINMUX_TOUCH_RESET              0x03001088UL
#define PINMUX_I2C4_VALUE               5U
#define PINMUX_PWR_GPIO_VALUE           3U
#define PWR_GPIO_UNLOCK3                0x05027078UL
#define PWR_GPIO_UNLOCK4                0x0502707cUL
#define PWR_GPIO_UNLOCK_VALUE           0x11U
#define PWR_GPIO_BASE                   0x05021000UL
#define PWR_GPIO_DATA                   (PWR_GPIO_BASE + 0x00)
#define PWR_GPIO_DIRECTION              (PWR_GPIO_BASE + 0x04)
#define PWR_GPIO_EXT_PORT               (PWR_GPIO_BASE + 0x50)
#define TOUCH_IRQ_MASK                  (1U << 3)
#define TOUCH_RESET_MASK                (1U << 4)

#define CST816T_REG_CHIP_ID             0xa7U
#define CST816T_REG_FW_VERSION          0xa9U
#define CST816T_REG_IRQ_CONTROL         0xfaU
#define CST816T_REG_DIS_AUTO_SLEEP      0xfeU
#define CST816T_IRQ_CONTINUOUS          0x60U
#define CST816T_AUTO_SLEEP_DISABLED     0x01U

#define I2C_TIMEOUT_LOOPS               2000000U
#define RESET_LOW_DELAY_CYCLES          10000000ULL
#define RESET_HIGH_DELAY_CYCLES         100000000ULL
#define RECOVERY_SETTLE_CYCLES          500000ULL
#define FALLBACK_POLL_CYCLES            20000000ULL
#define MOVE_LOG_THRESHOLD              2U
#define ACTIVE_REFRESH_EVENTS           25U
#define TOUCH_COORDINATE_MAX            239U

#define TOUCH_OWNER_PINMUX_SCL          (1U << 0)
#define TOUCH_OWNER_PINMUX_SDA          (1U << 1)
#define TOUCH_OWNER_PINMUX_IRQ          (1U << 2)
#define TOUCH_OWNER_PINMUX_RESET        (1U << 3)
#define TOUCH_OWNER_IRQ_DIRECTION       (1U << 4)
#define TOUCH_OWNER_RESET_DIRECTION     (1U << 5)
#define TOUCH_OWNER_RESET_LEVEL         (1U << 6)
#define TOUCH_OWNER_CLOCK               (1U << 7)
#define TOUCH_OWNER_RESET_CONTROL       (1U << 8)

static uint64_t last_poll_cycles;
static uint32_t previous_points;
static uint32_t previous_event;
static uint32_t previous_gesture;
static uint32_t previous_x;
static uint32_t previous_y;
static uint32_t active_silent_count;
static int previous_valid;

static inline uint32_t mmio_read(uintptr_t address)
{
        return *(volatile uint32_t *)address;
}

static inline void mmio_write(uintptr_t address, uint32_t value)
{
        *(volatile uint32_t *)address = value;
}

static inline void memory_barrier(void)
{
        __asm__ volatile ("fence rw, rw" ::: "memory");
}

static inline uint64_t read_cycle(void)
{
        uint64_t value;

        __asm__ volatile ("rdcycle %0" : "=r" (value));
        return value;
}

static void delay_cycles(uint64_t cycles)
{
        uint64_t start = read_cycle();

        while (read_cycle() - start < cycles)
                __asm__ volatile ("nop");
}

static uint32_t touch_irq_level(void)
{
        return (mmio_read(PWR_GPIO_EXT_PORT) & TOUCH_IRQ_MASK) ? 1U : 0U;
}

static uint32_t capture_ownership(struct airlink_touch_status *status)
{
        uint32_t mismatch = 0U;

        status->pinmux_scl = mmio_read(PINMUX_I2C4_SCL);
        status->pinmux_sda = mmio_read(PINMUX_I2C4_SDA);
        status->pinmux_irq = mmio_read(PINMUX_TOUCH_IRQ);
        status->pinmux_reset = mmio_read(PINMUX_TOUCH_RESET);
        status->gpio_data = mmio_read(PWR_GPIO_DATA);
        status->gpio_direction = mmio_read(PWR_GPIO_DIRECTION);
        status->gpio_ext_port = mmio_read(PWR_GPIO_EXT_PORT);
        status->clock_enable_1 = mmio_read(CLOCK_ENABLE_1);
        status->clock_enable_3 = mmio_read(CLOCK_ENABLE_3);
        status->reset_state = mmio_read(RESET_CONTROL);

        if (status->pinmux_scl != PINMUX_I2C4_VALUE)
                mismatch |= TOUCH_OWNER_PINMUX_SCL;
        if (status->pinmux_sda != PINMUX_I2C4_VALUE)
                mismatch |= TOUCH_OWNER_PINMUX_SDA;
        if (status->pinmux_irq != PINMUX_PWR_GPIO_VALUE)
                mismatch |= TOUCH_OWNER_PINMUX_IRQ;
        if (status->pinmux_reset != PINMUX_PWR_GPIO_VALUE)
                mismatch |= TOUCH_OWNER_PINMUX_RESET;
        if (status->gpio_direction & TOUCH_IRQ_MASK)
                mismatch |= TOUCH_OWNER_IRQ_DIRECTION;
        if ((status->gpio_direction & TOUCH_RESET_MASK) == 0U)
                mismatch |= TOUCH_OWNER_RESET_DIRECTION;
        if ((status->gpio_data & TOUCH_RESET_MASK) == 0U)
                mismatch |= TOUCH_OWNER_RESET_LEVEL;
        if ((status->clock_enable_1 & CLOCK_APB_I2C_ENABLE) == 0U ||
            (status->clock_enable_3 &
             (CLOCK_I2C_ENABLE | CLOCK_APB_I2C4_ENABLE)) !=
             (CLOCK_I2C_ENABLE | CLOCK_APB_I2C4_ENABLE))
                mismatch |= TOUCH_OWNER_CLOCK;
        if ((status->reset_state & RESET_I2C4_DEASSERT) == 0U)
                mismatch |= TOUCH_OWNER_RESET_CONTROL;
        status->ownership_mismatch = mismatch;
        return mismatch;
}

static void capture_i2c_debug(struct airlink_touch_status *status)
{
        status->i2c_status = mmio_read(IC_STATUS);
        status->i2c_abort_source = mmio_read(IC_TX_ABRT_SOURCE);
}

static int fail(struct airlink_touch_status *status, uint32_t error, uint32_t reg)
{
        status->last_error = error;
        status->last_register = reg;
        status->error_count++;
        capture_i2c_debug(status);
        if (error == AIRLINK_TOUCH_ERROR_TX_ABORT)
                (void)mmio_read(IC_CLR_TX_ABRT);
        return -1;
}

static void touch_claim_gpio(int pulse_reset)
{
        uint32_t direction;
        uint32_t data;

        mmio_write(PINMUX_I2C4_SCL, PINMUX_I2C4_VALUE);
        mmio_write(PINMUX_I2C4_SDA, PINMUX_I2C4_VALUE);
        mmio_write(PINMUX_TOUCH_IRQ, PINMUX_PWR_GPIO_VALUE);
        mmio_write(PINMUX_TOUCH_RESET, PINMUX_PWR_GPIO_VALUE);
        mmio_write(PWR_GPIO_UNLOCK3, PWR_GPIO_UNLOCK_VALUE);
        mmio_write(PWR_GPIO_UNLOCK4, PWR_GPIO_UNLOCK_VALUE);

        direction = mmio_read(PWR_GPIO_DIRECTION);
        direction &= ~TOUCH_IRQ_MASK;
        direction |= TOUCH_RESET_MASK;
        mmio_write(PWR_GPIO_DIRECTION, direction);

        data = mmio_read(PWR_GPIO_DATA);
        if (pulse_reset) {
                mmio_write(PWR_GPIO_DATA, data & ~TOUCH_RESET_MASK);
                memory_barrier();
                delay_cycles(RESET_LOW_DELAY_CYCLES);
        }
        mmio_write(PWR_GPIO_DATA, data | TOUCH_RESET_MASK);
        memory_barrier();
        if (pulse_reset)
                delay_cycles(RESET_HIGH_DELAY_CYCLES);
        else
                delay_cycles(RECOVERY_SETTLE_CYCLES);
}

static int i2c_set_enable(struct airlink_touch_status *status, uint32_t enable)
{
        for (uint32_t timeout = I2C_TIMEOUT_LOOPS; timeout != 0U; --timeout) {
                mmio_write(IC_ENABLE, enable);
                if ((mmio_read(IC_ENABLE_STATUS) & 1U) == enable)
                        return 0;
        }
        return fail(status, enable ? AIRLINK_TOUCH_ERROR_ENABLE_TIMEOUT :
                    AIRLINK_TOUCH_ERROR_DISABLE_TIMEOUT, 0xffffffffU);
}

static int i2c_wait_idle(struct airlink_touch_status *status, uint32_t reg)
{
        for (uint32_t timeout = I2C_TIMEOUT_LOOPS; timeout != 0U; --timeout) {
                uint32_t value = mmio_read(IC_STATUS);

                if (!(value & (IC_STATUS_ACTIVITY | IC_STATUS_MASTER_ACTIVITY)) &&
                    (value & IC_STATUS_TFE))
                        return 0;
                if (mmio_read(IC_RAW_INTR_STAT) & IC_INTR_TX_ABRT)
                        return fail(status, AIRLINK_TOUCH_ERROR_TX_ABORT, reg);
        }
        return fail(status, AIRLINK_TOUCH_ERROR_BUS_BUSY, reg);
}

static int i2c_wait_tx_space(struct airlink_touch_status *status, uint32_t reg)
{
        for (uint32_t timeout = I2C_TIMEOUT_LOOPS; timeout != 0U; --timeout) {
                if (mmio_read(IC_RAW_INTR_STAT) & IC_INTR_TX_ABRT)
                        return fail(status, AIRLINK_TOUCH_ERROR_TX_ABORT, reg);
                if (mmio_read(IC_STATUS) & IC_STATUS_TFNF)
                        return 0;
        }
        return fail(status, AIRLINK_TOUCH_ERROR_TX_TIMEOUT, reg);
}

static void i2c_prepare_transaction(void)
{
        while ((mmio_read(IC_STATUS) & IC_STATUS_RFNE) || mmio_read(IC_RXFLR) != 0U)
                (void)mmio_read(IC_DATA_CMD);
        (void)mmio_read(IC_CLR_INTR);
}

static int i2c_finish_transaction(struct airlink_touch_status *status, uint32_t reg)
{
        for (uint32_t timeout = I2C_TIMEOUT_LOOPS; timeout != 0U; --timeout) {
                uint32_t raw = mmio_read(IC_RAW_INTR_STAT);

                if (raw & IC_INTR_TX_ABRT)
                        return fail(status, AIRLINK_TOUCH_ERROR_TX_ABORT, reg);
                if (raw & IC_INTR_STOP_DET) {
                        (void)mmio_read(IC_CLR_STOP_DET);
                        return i2c_wait_idle(status, reg);
                }
        }
        return fail(status, AIRLINK_TOUCH_ERROR_STOP_TIMEOUT, reg);
}

static int i2c_write_register(struct airlink_touch_status *status,
                              uint8_t reg, uint8_t value)
{
        if (i2c_wait_idle(status, reg) != 0)
                return -1;
        i2c_prepare_transaction();
        if (i2c_wait_tx_space(status, reg) != 0)
                return -1;
        mmio_write(IC_DATA_CMD, reg);
        if (i2c_wait_tx_space(status, reg) != 0)
                return -1;
        mmio_write(IC_DATA_CMD, (uint32_t)value | IC_DATA_CMD_STOP);
        return i2c_finish_transaction(status, reg);
}

static int i2c_read_registers(struct airlink_touch_status *status,
                              uint8_t reg, uint8_t *buffer, uint32_t length)
{
        uint32_t received = 0U;

        if (length == 0U)
                return 0;
        if (i2c_wait_idle(status, reg) != 0)
                return -1;
        i2c_prepare_transaction();
        if (i2c_wait_tx_space(status, reg) != 0)
                return -1;
        mmio_write(IC_DATA_CMD, reg);

        for (uint32_t index = 0U; index < length; ++index) {
                uint32_t command = IC_DATA_CMD_READ;

                if (index == 0U)
                        command |= IC_DATA_CMD_RESTART;
                if (index + 1U == length)
                        command |= IC_DATA_CMD_STOP;
                if (i2c_wait_tx_space(status, reg) != 0)
                        return -1;
                mmio_write(IC_DATA_CMD, command);
        }

        for (uint32_t timeout = I2C_TIMEOUT_LOOPS;
             timeout != 0U && received < length; --timeout) {
                if (mmio_read(IC_RAW_INTR_STAT) & IC_INTR_TX_ABRT)
                        return fail(status, AIRLINK_TOUCH_ERROR_TX_ABORT, reg);
                if ((mmio_read(IC_STATUS) & IC_STATUS_RFNE) ||
                    mmio_read(IC_RXFLR) != 0U) {
                        buffer[received++] = (uint8_t)mmio_read(IC_DATA_CMD);
                        timeout = I2C_TIMEOUT_LOOPS;
                }
        }
        if (received != length)
                return fail(status, AIRLINK_TOUCH_ERROR_RX_TIMEOUT, reg);
        return i2c_finish_transaction(status, reg);
}

static int i2c_platform_prepare(struct airlink_touch_status *status)
{
        uint32_t value;

        value = mmio_read(CLOCK_ENABLE_1) | CLOCK_APB_I2C_ENABLE;
        mmio_write(CLOCK_ENABLE_1, value);
        value = mmio_read(CLOCK_ENABLE_3) |
                CLOCK_I2C_ENABLE | CLOCK_APB_I2C4_ENABLE;
        mmio_write(CLOCK_ENABLE_3, value);

        /*
         * CVITEK reset bits are active low. Only release I2C4 from reset;
         * do not pulse reset because an earlier boot stage may have already
         * configured a live controller.
         */
        value = mmio_read(RESET_CONTROL) | RESET_I2C4_DEASSERT;
        mmio_write(RESET_CONTROL, value);
        memory_barrier();

        status->clock_enable_1 = mmio_read(CLOCK_ENABLE_1);
        status->clock_enable_3 = mmio_read(CLOCK_ENABLE_3);
        status->reset_state = mmio_read(RESET_CONTROL);
        status->clock_divider = mmio_read(CLOCK_I2C_DIV);
        status->clock_bypass = mmio_read(CLOCK_BYPASS_0);

        if ((status->clock_enable_1 & CLOCK_APB_I2C_ENABLE) == 0U ||
            (status->clock_enable_3 &
             (CLOCK_I2C_ENABLE | CLOCK_APB_I2C4_ENABLE)) !=
             (CLOCK_I2C_ENABLE | CLOCK_APB_I2C4_ENABLE) ||
            (status->reset_state & RESET_I2C4_DEASSERT) == 0U)
                return fail(status, AIRLINK_TOUCH_ERROR_PLATFORM,
                            0xffffffffU);
        return 0;
}

static int i2c_controller_init(struct airlink_touch_status *status)
{
        status->component_type = mmio_read(IC_COMP_TYPE);
        if (status->component_type != I2C_COMPONENT_TYPE)
                return fail(status, AIRLINK_TOUCH_ERROR_COMPONENT, 0xffffffffU);

        if (i2c_set_enable(status, 0U) != 0)
                return -1;
        mmio_write(IC_INTR_MASK, 0U);
        mmio_write(IC_CON, IC_CON_R14_VALUE);
        mmio_write(IC_TAR, I2C_TARGET_ADDRESS);
        mmio_write(IC_SS_SCL_HCNT, I2C_SS_HCNT_100MHZ);
        mmio_write(IC_SS_SCL_LCNT, I2C_SS_LCNT_100MHZ);
        mmio_write(IC_RX_TL, 0U);
        mmio_write(IC_TX_TL, 0U);
        (void)mmio_read(IC_CLR_INTR);
        if (i2c_set_enable(status, 1U) != 0)
                return -1;
        return i2c_wait_idle(status, 0xffffffffU);
}

static int touch_configure_device(struct airlink_touch_status *status)
{
        uint8_t value;

        if (i2c_read_registers(status, CST816T_REG_CHIP_ID, &value, 1U) != 0)
                return -1;
        status->chip_id = value;
        if (i2c_read_registers(status, CST816T_REG_FW_VERSION, &value, 1U) != 0)
                return -1;
        status->firmware_version = value;

        if (i2c_write_register(status, CST816T_REG_IRQ_CONTROL,
                               CST816T_IRQ_CONTINUOUS) != 0)
                return -1;
        if (i2c_read_registers(status, CST816T_REG_IRQ_CONTROL, &value, 1U) != 0)
                return -1;
        status->irq_control = value;
        if (value != CST816T_IRQ_CONTINUOUS)
                return fail(status, AIRLINK_TOUCH_ERROR_CONFIG,
                            CST816T_REG_IRQ_CONTROL);

        /* Keep the always-lit user interface responsive without a wake touch. */
        if (i2c_write_register(status, CST816T_REG_DIS_AUTO_SLEEP,
                               CST816T_AUTO_SLEEP_DISABLED) != 0)
                return -1;
        status->auto_sleep_control = CST816T_AUTO_SLEEP_DISABLED;
        return 0;
}

static int i2c_recover_controller(struct airlink_touch_status *status)
{
        touch_claim_gpio(0);
        if (i2c_platform_prepare(status) != 0)
                return -1;
        if (i2c_controller_init(status) != 0)
                return -1;
        status->recovery_count++;
        return 0;
}

static int touch_reinitialise_device(struct airlink_touch_status *status)
{
        touch_claim_gpio(1);
        if (i2c_platform_prepare(status) != 0)
                return -1;
        if (i2c_controller_init(status) != 0)
                return -1;
        if (touch_configure_device(status) != 0)
                return -1;
        status->reinit_count++;
        status->ready = 1U;
        previous_valid = 0;
        active_silent_count = 0U;
        last_poll_cycles = read_cycle();
        (void)capture_ownership(status);
        return 0;
}

static uint32_t absolute_difference(uint32_t left, uint32_t right)
{
        return left > right ? left - right : right - left;
}

static uint32_t clamp_coordinate(uint32_t value)
{
        return value > TOUCH_COORDINATE_MAX ? TOUCH_COORDINATE_MAX : value;
}

static void map_coordinates(struct airlink_touch_status *status)
{
        uint32_t raw_x = clamp_coordinate(status->raw_x);
        uint32_t raw_y = clamp_coordinate(status->raw_y);

        /*
         * Hardware five-point test:
         *   physical top    -> raw X high
         *   physical right  -> raw Y high
         *   physical bottom -> raw X low
         *   physical left   -> raw Y low
         *
         * Convert to the LVGL convention: top-left=(0,0), with positive
         * coordinates extending right and down.
         */
        status->x = raw_y;
        status->y = TOUCH_COORDINATE_MAX - raw_x;
}

int airlink_touch_init(struct airlink_touch_status *status)
{
        for (uint32_t index = 0U; index < sizeof(*status) / sizeof(uint32_t); ++index)
                ((uint32_t *)status)[index] = 0U;
        status->init_stage = 1U;
        if (i2c_platform_prepare(status) != 0)
                return -1;
        status->init_stage = 2U;

        touch_claim_gpio(1);
        status->irq_level = touch_irq_level();
        status->init_stage = 3U;

        if (i2c_controller_init(status) != 0)
                return -1;
        status->init_stage = 4U;

        if (touch_configure_device(status) != 0)
                return -1;
        status->init_stage = 5U;

        status->ready = 1U;
        status->init_stage = 6U;
        status->last_error = AIRLINK_TOUCH_ERROR_NONE;
        status->irq_level = touch_irq_level();
        last_poll_cycles = read_cycle();
        previous_valid = 0;
        (void)capture_ownership(status);
        return 0;
}

void airlink_touch_check_ownership(struct airlink_touch_status *status)
{
        status->owner_changed = capture_ownership(status) != 0U;
}

int airlink_touch_poll(struct airlink_touch_status *status)
{
        uint8_t buffer[7];
        uint32_t movement;
        uint32_t saved_error;
        uint32_t saved_register;
        uint32_t saved_abort;
        uint32_t saved_i2c_status;
        int should_log = 0;

        if (!status->ready)
                return -1;

        status->irq_level = touch_irq_level();
        uint64_t now = read_cycle();
        if (status->irq_level != 0U &&
            now - last_poll_cycles < FALLBACK_POLL_CYCLES)
                return 0;
        status->source = status->irq_level == 0U ?
                AIRLINK_TOUCH_SOURCE_IRQ : AIRLINK_TOUCH_SOURCE_POLL;
        last_poll_cycles = now;
        status->poll_count++;

        if (capture_ownership(status) != 0U) {
                uint32_t mismatch = status->ownership_mismatch;

                status->owner_changed = 1U;
                status->last_owner_mismatch = mismatch;
                if (touch_reinitialise_device(status) != 0) {
                        status->error_flags |= 1U;
                        status->last_error = AIRLINK_TOUCH_ERROR_OWNER;
                        status->error_count++;
                        return -1;
                }

                /* A successful boot-time reclaim is history, not a fault. */
                status->warning_flags |=
                        AIRLINK_TOUCH_WARNING_OWNER_RECOVERED;
                status->warning_count++;
                status->owner_recovery_count++;
                status->owner_changed = 0U;
                status->ownership_mismatch = 0U;
                status->last_error = AIRLINK_TOUCH_ERROR_NONE;
                return AIRLINK_TOUCH_RESULT_OWNER_RECOVERED;
        }

        if (i2c_read_registers(status, 0x00U, buffer, sizeof(buffer)) != 0) {
                saved_error = status->last_error;
                saved_register = status->last_register;
                saved_abort = status->i2c_abort_source;
                saved_i2c_status = status->i2c_status;

                /* TX_ABRT must flush the DW-I2C FIFO; CLR_TX_ABRT alone
                 * left R10 permanently at IC_STATUS=0x2. */
                if (i2c_recover_controller(status) == 0 &&
                    saved_error == AIRLINK_TOUCH_ERROR_TX_ABORT &&
                    (saved_abort & 1U) != 0U)
                        (void)touch_reinitialise_device(status);

                status->last_error = saved_error;
                status->last_register = saved_register;
                status->i2c_abort_source = saved_abort;
                status->i2c_status = saved_i2c_status;
                return -1;
        }
        status->successful_poll_count++;
        for (uint32_t index = 0U; index < sizeof(buffer); ++index)
                status->raw[index] = buffer[index];

        status->gesture = buffer[1];
        status->points = buffer[2] & 0x0fU;
        status->event = (buffer[3] >> 6) & 0x03U;
        status->raw_x = ((uint32_t)(buffer[3] & 0x0fU) << 8) | buffer[4];
        status->raw_y = ((uint32_t)(buffer[5] & 0x0fU) << 8) | buffer[6];
        map_coordinates(status);
        movement = absolute_difference(status->x, previous_x) +
                absolute_difference(status->y, previous_y);

        if (!previous_valid) {
                should_log = status->points != 0U || status->gesture != 0U;
        } else if (status->points != previous_points ||
                   status->event != previous_event ||
                   status->gesture != previous_gesture) {
                should_log = 1;
        } else if (status->points != 0U && movement >= MOVE_LOG_THRESHOLD) {
                should_log = 1;
        } else if (status->points != 0U &&
                   ++active_silent_count >= ACTIVE_REFRESH_EVENTS) {
                should_log = 1;
        }

        if (should_log) {
                status->event_count++;
                active_silent_count = 0U;
        }
        previous_points = status->points;
        previous_event = status->event;
        previous_gesture = status->gesture;
        previous_x = status->x;
        previous_y = status->y;
        previous_valid = 1;
        return should_log ? AIRLINK_TOUCH_RESULT_EVENT :
                AIRLINK_TOUCH_RESULT_NONE;
}

const char *airlink_touch_error_name(uint32_t error)
{
        switch (error) {
        case AIRLINK_TOUCH_ERROR_NONE: return "NONE";
        case AIRLINK_TOUCH_ERROR_COMPONENT: return "COMPONENT_MISMATCH";
        case AIRLINK_TOUCH_ERROR_DISABLE_TIMEOUT: return "DISABLE_TIMEOUT";
        case AIRLINK_TOUCH_ERROR_ENABLE_TIMEOUT: return "ENABLE_TIMEOUT";
        case AIRLINK_TOUCH_ERROR_BUS_BUSY: return "BUS_BUSY";
        case AIRLINK_TOUCH_ERROR_TX_TIMEOUT: return "TX_TIMEOUT";
        case AIRLINK_TOUCH_ERROR_RX_TIMEOUT: return "READ_TIMEOUT";
        case AIRLINK_TOUCH_ERROR_STOP_TIMEOUT: return "STOP_TIMEOUT";
        case AIRLINK_TOUCH_ERROR_TX_ABORT: return "TX_ABORT";
        case AIRLINK_TOUCH_ERROR_CONFIG: return "CONFIG_MISMATCH";
        case AIRLINK_TOUCH_ERROR_OWNER: return "OWNER_CHANGED";
        case AIRLINK_TOUCH_ERROR_PLATFORM: return "PLATFORM_NOT_READY";
        default: return "UNKNOWN";
        }
}

const char *airlink_touch_source_name(uint32_t source)
{
        switch (source) {
        case AIRLINK_TOUCH_SOURCE_IRQ: return "IRQ";
        case AIRLINK_TOUCH_SOURCE_POLL: return "POLL";
        default: return "NONE";
        }
}

const char *airlink_touch_event_name(uint32_t event, uint32_t points)
{
        if (points == 0U)
                return "RELEASE";
        switch (event) {
        case 0U: return "DOWN";
        case 1U: return "UP";
        case 2U: return "CONTACT";
        default: return "RESERVED";
        }
}
