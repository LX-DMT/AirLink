#include <stdint.h>

#include "ch347.h"

#define PINMUX_CH347_DTR        0x03001064UL /* JTAG_CPU_TMS */
#define PINMUX_CH347_RESET      0x03001068UL /* JTAG_CPU_TCK */
#define PINMUX_CH347_RTS        0x03001070UL /* IIC0_SCL */
#define PINMUX_GPIO_FUNCTION    3U

#define GPIOA_BASE              0x03020000UL
#define GPIO_OUT                (GPIOA_BASE + 0x00)
#define GPIO_OE                 (GPIOA_BASE + 0x04)
#define GPIO_EXT                (GPIOA_BASE + 0x50)

#define CH347_RESET_MASK        (1U << 18)
#define CH347_DTR_MASK          (1U << 19)
#define CH347_RTS_MASK          (1U << 28)
#define CH347_OUTPUT_MASK       (CH347_RESET_MASK | CH347_DTR_MASK | CH347_RTS_MASK)

#define TIMEBASE_HZ             25000000ULL
#define TICKS_PER_MS            (TIMEBASE_HZ / 1000ULL)
#define RESET_PULSE_MS          80U

struct mode_levels { uint8_t dtr; uint8_t rts; };

static const struct mode_levels mode_table[AIRLINK_CH347_MODE_COUNT] = {
    [AIRLINK_CH347_DUAL_UART] = {1U, 1U},
    [AIRLINK_CH347_SPI_I2C] = {1U, 0U},
    [AIRLINK_CH347_SPI_I2C_DRIVERLESS] = {0U, 1U},
    [AIRLINK_CH347_JTAG_UART] = {0U, 0U},
};

static inline uint32_t mmio_read(uintptr_t address) { return *(volatile uint32_t *)address; }
static inline void mmio_write(uintptr_t address, uint32_t value) { *(volatile uint32_t *)address = value; }
static inline void memory_barrier(void) { __asm__ volatile ("fence rw, rw" ::: "memory"); }
static inline uint64_t read_time(void) { uint64_t value; __asm__ volatile ("rdtime %0" : "=r" (value)); return value; }

static void delay_ms(uint32_t milliseconds)
{
    uint64_t deadline = read_time() + (uint64_t)milliseconds * TICKS_PER_MS;
    while ((int64_t)(read_time() - deadline) < 0) __asm__ volatile ("nop");
}

static uint32_t level_from_mask(uint32_t value, uint32_t mask)
{
    return (value & mask) != 0U ? 1U : 0U;
}

static uint32_t mode_from_levels(uint32_t dtr, uint32_t rts)
{
    for (uint32_t mode = 0U; mode < AIRLINK_CH347_MODE_COUNT; ++mode)
        if (mode_table[mode].dtr == dtr && mode_table[mode].rts == rts) return mode;
    return AIRLINK_CH347_DUAL_UART;
}

static void capture_status(struct airlink_ch347_status *status)
{
    status->pinmux_reset = mmio_read(PINMUX_CH347_RESET);
    status->pinmux_dtr = mmio_read(PINMUX_CH347_DTR);
    status->pinmux_rts = mmio_read(PINMUX_CH347_RTS);
    status->gpio_out = mmio_read(GPIO_OUT);
    status->gpio_oe = mmio_read(GPIO_OE);
    status->gpio_ext = mmio_read(GPIO_EXT);
    status->reset_level = level_from_mask(status->gpio_out, CH347_RESET_MASK);
}

static uint32_t ownership_mismatch(const struct airlink_ch347_status *status)
{
    uint32_t mismatch = 0U;
    if (status->pinmux_reset != PINMUX_GPIO_FUNCTION) mismatch |= AIRLINK_CH347_OWNER_PINMUX_RESET;
    if (status->pinmux_dtr != PINMUX_GPIO_FUNCTION) mismatch |= AIRLINK_CH347_OWNER_PINMUX_DTR;
    if (status->pinmux_rts != PINMUX_GPIO_FUNCTION) mismatch |= AIRLINK_CH347_OWNER_PINMUX_RTS;
    if ((status->gpio_oe & CH347_OUTPUT_MASK) != CH347_OUTPUT_MASK) mismatch |= AIRLINK_CH347_OWNER_DIRECTION;
    if ((status->gpio_out & CH347_RESET_MASK) == 0U) mismatch |= AIRLINK_CH347_OWNER_RESET_LEVEL;
    if (level_from_mask(status->gpio_out, CH347_DTR_MASK) != status->dtr_level ||
        level_from_mask(status->gpio_out, CH347_RTS_MASK) != status->rts_level)
        mismatch |= AIRLINK_CH347_OWNER_MODE_LEVELS;
    return mismatch;
}

static void write_mode_levels(uint32_t mode, uint32_t reset_high)
{
    uint32_t output = mmio_read(GPIO_OUT);
    output &= ~(CH347_RESET_MASK | CH347_DTR_MASK | CH347_RTS_MASK);
    if (reset_high) output |= CH347_RESET_MASK;
    if (mode_table[mode].dtr) output |= CH347_DTR_MASK;
    if (mode_table[mode].rts) output |= CH347_RTS_MASK;
    mmio_write(GPIO_OUT, output);
    memory_barrier();
}

static void claim_pins(uint32_t mode)
{
    /* Preload safe levels before changing pinmux or output-enable. */
    write_mode_levels(mode, 1U);
    mmio_write(PINMUX_CH347_DTR, PINMUX_GPIO_FUNCTION);
    mmio_write(PINMUX_CH347_RESET, PINMUX_GPIO_FUNCTION);
    mmio_write(PINMUX_CH347_RTS, PINMUX_GPIO_FUNCTION);
    memory_barrier();
    mmio_write(GPIO_OE, mmio_read(GPIO_OE) | CH347_OUTPUT_MASK);
    memory_barrier();
}

int airlink_ch347_init(struct airlink_ch347_status *status)
{
    uint32_t pad_levels = mmio_read(GPIO_EXT);
    uint32_t initial_dtr = level_from_mask(pad_levels, CH347_DTR_MASK);
    uint32_t initial_rts = level_from_mask(pad_levels, CH347_RTS_MASK);
    uint32_t initial_mode = mode_from_levels(initial_dtr, initial_rts);

    for (uint32_t index = 0U; index < sizeof(*status) / sizeof(uint32_t); ++index)
        ((uint32_t *)status)[index] = 0U;
    status->current_mode = initial_mode;
    status->dtr_level = initial_dtr;
    status->rts_level = initial_rts;
    status->pulse_ms = RESET_PULSE_MS;
    claim_pins(initial_mode);
    capture_status(status);
    status->ownership_mismatch = ownership_mismatch(status);
    if (status->ownership_mismatch != 0U) {
        status->error_flags |= AIRLINK_CH347_ERROR_PINMUX | AIRLINK_CH347_ERROR_DIRECTION;
        return -1;
    }
    status->ready = 1U;
    return 0;
}

int airlink_ch347_begin_mode(struct airlink_ch347_status *status,
                             uint32_t mode, uint64_t now)
{
    uint32_t output;
    if (mode >= AIRLINK_CH347_MODE_COUNT || status->switching) {
        status->error_flags |= AIRLINK_CH347_ERROR_INVALID_MODE;
        return -1;
    }
    /*
     * Suspend the periodic ownership monitor before asserting reset.  The
     * monitor normally requires RESET# high and would otherwise interpret the
     * intentional 80 ms low pulse as pin theft, restore current_mode and race
     * the pending switch.
     */
    status->ready = 0U;
    status->last_switch_mismatch = 0U;
    status->dtr_level = mode_table[mode].dtr;
    status->rts_level = mode_table[mode].rts;
    status->pending_mode = mode;
    claim_pins(mode);
    output = mmio_read(GPIO_OUT) & ~CH347_RESET_MASK;
    mmio_write(GPIO_OUT, output);
    memory_barrier();
    status->reset_level = 0U;
    status->release_deadline = now + (uint64_t)RESET_PULSE_MS * TICKS_PER_MS;
    status->switching = 1U;
    return 0;
}

int airlink_ch347_tick(struct airlink_ch347_status *status, uint64_t now)
{
    uint32_t output;
    if (!status->switching)
        return 0;
    if ((int64_t)(now - status->release_deadline) < 0)
        return 0;
    output = mmio_read(GPIO_OUT) | CH347_RESET_MASK;
    mmio_write(GPIO_OUT, output);
    memory_barrier();
    status->current_mode = status->pending_mode;
    capture_status(status);
    status->ownership_mismatch = ownership_mismatch(status);
    if (status->ownership_mismatch != 0U) {
        /*
         * A bus/pinmux write can occasionally overlap the release readback.
         * Re-assert the complete pending-mode ownership once before treating
         * it as a real hardware failure.
         */
        status->last_switch_mismatch = status->ownership_mismatch;
        claim_pins(status->pending_mode);
        capture_status(status);
        status->ownership_mismatch = ownership_mismatch(status);
        if (status->ownership_mismatch != 0U) {
            status->error_flags |= AIRLINK_CH347_ERROR_OUTPUT_READBACK;
            status->switching = 0U;
            status->ready = 1U;
            return -1;
        }
        status->switch_recovery_count++;
        status->recovery_count++;
    }
    status->switch_count++;
    status->switching = 0U;
    status->ready = 1U;
    return 1;
}

int airlink_ch347_apply_mode(struct airlink_ch347_status *status, uint32_t mode)
{
    uint64_t now = read_time();
    int result = airlink_ch347_begin_mode(status, mode, now);
    if (result != 0) return result;
    delay_ms(RESET_PULSE_MS);
    return airlink_ch347_tick(status, read_time()) < 0 ? -1 : 0;
}

int airlink_ch347_check_ownership(struct airlink_ch347_status *status)
{
    uint32_t expected_dtr;
    uint32_t expected_rts;
    uint32_t mismatch;

    if (status->switching || !status->ready)
        return 0;

    expected_dtr = status->dtr_level;
    expected_rts = status->rts_level;
    capture_status(status);
    status->dtr_level = expected_dtr;
    status->rts_level = expected_rts;
    mismatch = ownership_mismatch(status);
    status->ownership_mismatch = mismatch;
    if (mismatch == 0U) return 0;
    status->last_owner_mismatch = mismatch;
    claim_pins(status->current_mode);
    capture_status(status);
    status->ownership_mismatch = ownership_mismatch(status);
    if (status->ownership_mismatch != 0U) {
        status->error_flags |= AIRLINK_CH347_ERROR_PINMUX | AIRLINK_CH347_ERROR_DIRECTION;
        return -1;
    }
    status->recovery_count++;
    return 1;
}

const char *airlink_ch347_mode_name(uint32_t mode)
{
    switch (mode) {
    case AIRLINK_CH347_DUAL_UART: return "DUAL_UART";
    case AIRLINK_CH347_SPI_I2C: return "SPI_I2C";
    case AIRLINK_CH347_SPI_I2C_DRIVERLESS: return "SPI_I2C_DRIVERLESS";
    case AIRLINK_CH347_JTAG_UART: return "JTAG_UART";
    default: return "INVALID";
    }
}