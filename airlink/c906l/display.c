#include <stddef.h>
#include <stdint.h>

#include "display.h"

#define PINMUX_BASE             0x03001000UL
#define PINMUX_MIPI_TXM2        (PINMUX_BASE + 0x1a4)
#define PINMUX_MIPI_TXP2        (PINMUX_BASE + 0x1a8)
#define PINMUX_MIPI_TXM1        (PINMUX_BASE + 0x1ac)
#define PINMUX_MIPI_TXP1        (PINMUX_BASE + 0x1b0)
#define PINMUX_GPIOA15          (PINMUX_BASE + 0x03c)
#define PINMUX_GPIOA27          (PINMUX_BASE + 0x058)
#define PINMUX_SPI_FUNCTION     6U
#define PINMUX_GPIO_FUNCTION    3U

#define CLOCK_BASE              0x03002000UL
#define CLOCK_ENABLE_3          (CLOCK_BASE + 0x00c)
#define CLOCK_SPI_ENABLE        (1U << 6)
#define CLOCK_BYPASS_0          (CLOCK_BASE + 0x030)
#define CLOCK_BYPASS_1          (CLOCK_BASE + 0x034)
#define CLOCK_SPI_DIV           (CLOCK_BASE + 0x100)
#define CLOCK_DIV_SHIFT         16U
#define CLOCK_DIV_MASK          (0x3fU << CLOCK_DIV_SHIFT)
#define CLOCK_DIV_APPLY         (1U << 3)
#define CLOCK_DIV_ENABLE        (1U << 0)
#define CLOCK_DIV_SAFE          8U
#define FPLL_HZ                 1500000000U

#define GPIOA_BASE              0x03020000UL
#define GPIO_OUT                (GPIOA_BASE + 0x00)
#define GPIO_OE                 (GPIOA_BASE + 0x04)
#define LCD_DC_MASK             (1U << 15)
#define LCD_RESET_MASK          (1U << 27)

#define SPI0_BASE               0x04180000UL
#define SPI_CTRLR0              (SPI0_BASE + 0x00)
#define SPI_CTRLR1              (SPI0_BASE + 0x04)
#define SPI_SSIENR              (SPI0_BASE + 0x08)
#define SPI_SER                 (SPI0_BASE + 0x10)
#define SPI_BAUDR               (SPI0_BASE + 0x14)
#define SPI_TXFTLR              (SPI0_BASE + 0x18)
#define SPI_RXFTLR              (SPI0_BASE + 0x1c)
#define SPI_TXFLR               (SPI0_BASE + 0x20)
#define SPI_RXFLR               (SPI0_BASE + 0x24)
#define SPI_SR                  (SPI0_BASE + 0x28)
#define SPI_IMR                 (SPI0_BASE + 0x2c)
#define SPI_ICR                 (SPI0_BASE + 0x48)
#define SPI_DMACR               (SPI0_BASE + 0x4c)
#define SPI_DR                  (SPI0_BASE + 0x60)

#define SPI_SR_BUSY             (1U << 0)
#define SPI_SR_TF_EMPTY         (1U << 2)
#define SPI_CTRLR0_MODE3_8BIT_TX_ONLY 0x000001c7U
#define SPI_DIVIDER_SAFE        4U
#define SPI_FIFO_MAX_DEPTH      256U
#define SPI_FIFO_FALLBACK_DEPTH 1U
#define SPI_DIAGNOSTIC(fifo, divider)    \
    (((fifo) << 24) | ((divider) << 16) | \
     SPI_CTRLR0_MODE3_8BIT_TX_ONLY)

struct panel_command {
    uint8_t command;
    uint8_t length;
    uint8_t data[12];
};

#include "gc9a01_init.inc"

#define PANEL_SIZE              240U
#define TIMEBASE_HZ             25000000ULL
#define TICKS_PER_MS            (TIMEBASE_HZ / 1000ULL)

static uint64_t panel_start_time;

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

static inline uint64_t read_time(void)
{
    uint64_t value;

    __asm__ volatile ("rdtime %0" : "=r" (value));
    return value;
}

static uint32_t elapsed_ms(uint64_t start)
{
    return (uint32_t)((read_time() - start) / TICKS_PER_MS);
}

static void delay_ms(uint32_t milliseconds)
{
    uint64_t deadline = read_time() +
        (uint64_t)milliseconds * TICKS_PER_MS;

    while ((int64_t)(read_time() - deadline) < 0)
        __asm__ volatile ("nop");
}

static void set_error(struct airlink_display_status *status, uint32_t error)
{
    status->error_flags |= error;
}

static void gpio_set(uint32_t mask, int high)
{
    uint32_t value = mmio_read(GPIO_OUT);

    if (high)
        value |= mask;
    else
        value &= ~mask;
    mmio_write(GPIO_OUT, value);
}

static void gpio_prepare_output(uint32_t mask, int initial_high)
{
    gpio_set(mask, initial_high);
    mmio_write(GPIO_OE, mmio_read(GPIO_OE) | mask);
}

static void pinmux_init(void)
{
    mmio_write(PINMUX_MIPI_TXM2, PINMUX_SPI_FUNCTION); /* SPI0_SCK */
    mmio_write(PINMUX_MIPI_TXP2, PINMUX_SPI_FUNCTION); /* SPI0_CS_X */
    mmio_write(PINMUX_MIPI_TXM1, PINMUX_SPI_FUNCTION); /* SPI0_SDO */
    mmio_write(PINMUX_MIPI_TXP1, PINMUX_SPI_FUNCTION); /* SPI0_SDI */
    mmio_write(PINMUX_GPIOA15, PINMUX_GPIO_FUNCTION);   /* LCD_DC */
    mmio_write(PINMUX_GPIOA27, PINMUX_GPIO_FUNCTION);   /* LCD_RESET */
    memory_barrier();
}

static uint32_t clock_div_value(uint32_t original, uint32_t divisor)
{
    return (original & ~CLOCK_DIV_MASK) |
        (divisor << CLOCK_DIV_SHIFT) | CLOCK_DIV_APPLY | CLOCK_DIV_ENABLE;
}

static int clock_set_divider(struct airlink_display_status *status,
                             uint32_t divisor)
{
    uint32_t original = mmio_read(CLOCK_SPI_DIV);
    uint32_t wanted = clock_div_value(original, divisor);
    uint32_t readback;

    mmio_write(CLOCK_SPI_DIV, wanted);
    memory_barrier();
    readback = mmio_read(CLOCK_SPI_DIV);
    status->clock_div_register = readback;
    return (readback & (CLOCK_DIV_MASK | CLOCK_DIV_APPLY |
                        CLOCK_DIV_ENABLE)) ==
           (wanted & (CLOCK_DIV_MASK | CLOCK_DIV_APPLY |
                      CLOCK_DIV_ENABLE)) ? 0 : -1;
}

static void clock_use_safe(struct airlink_display_status *status)
{
    (void)clock_set_divider(status, CLOCK_DIV_SAFE);
    status->spi_parent_hz = FPLL_HZ / CLOCK_DIV_SAFE;
    status->spi_sclk_hz = status->spi_parent_hz / SPI_DIVIDER_SAFE;
    status->spi_baudr = SPI_DIVIDER_SAFE;
    status->spi_high_speed = 0U;
    set_error(status, AIRLINK_DISPLAY_ERROR_CLOCK_FALLBACK);
}

static void clock_init(struct airlink_display_status *status)
{
    mmio_write(CLOCK_ENABLE_3,
               mmio_read(CLOCK_ENABLE_3) | CLOCK_SPI_ENABLE);
    memory_barrier();
    status->clock_bypass_register = mmio_read(CLOCK_BYPASS_0);
    status->clock_bypass_register_1 = mmio_read(CLOCK_BYPASS_1);
    if (clock_set_divider(status, CLOCK_DIV_SAFE) != 0)
        set_error(status, AIRLINK_DISPLAY_ERROR_CLOCK_FALLBACK);
    status->spi_parent_hz = FPLL_HZ / CLOCK_DIV_SAFE;
    status->spi_sclk_hz = status->spi_parent_hz / SPI_DIVIDER_SAFE;
    status->spi_baudr = SPI_DIVIDER_SAFE;
    status->spi_high_speed = 0U;
}

/* Match the FIFO-depth probing method used by Linux spi-dw-core.c. */
static uint32_t spi_detect_fifo_depth(void)
{
    uint32_t fifo;

    for (fifo = 1U; fifo < SPI_FIFO_MAX_DEPTH; ++fifo) {
        mmio_write(SPI_TXFTLR, fifo);
        if (mmio_read(SPI_TXFTLR) != fifo)
            break;
    }
    mmio_write(SPI_TXFTLR, 0U);

    if (fifo == 1U)
        return SPI_FIFO_FALLBACK_DEPTH;
    return fifo;
}

static void spi_init(struct airlink_display_status *status)
{
    uint32_t divider = SPI_DIVIDER_SAFE;

    mmio_write(SPI_SSIENR, 0U);
    mmio_write(SPI_SER, 0U);
    mmio_write(SPI_IMR, 0U);
    (void)mmio_read(SPI_ICR);
    mmio_write(SPI_DMACR, 0U);
    mmio_write(SPI_CTRLR0, SPI_CTRLR0_MODE3_8BIT_TX_ONLY);
    mmio_write(SPI_CTRLR1, 0U);
    mmio_write(SPI_BAUDR, divider);
    if (mmio_read(SPI_CTRLR0) != SPI_CTRLR0_MODE3_8BIT_TX_ONLY ||
        mmio_read(SPI_BAUDR) != divider) {
        clock_use_safe(status);
        divider = SPI_DIVIDER_SAFE;
        mmio_write(SPI_CTRLR0, SPI_CTRLR0_MODE3_8BIT_TX_ONLY);
        mmio_write(SPI_BAUDR, divider);
    }
    status->spi_baudr = divider;
    status->spi_sclk_hz = status->spi_parent_hz / divider;
    status->fifo_depth = spi_detect_fifo_depth();
    status->spi_diagnostic = SPI_DIAGNOSTIC(status->fifo_depth, divider);
    mmio_write(SPI_TXFTLR, 0U);
    mmio_write(SPI_RXFTLR, 0U);
    mmio_write(SPI_SSIENR, 1U);
    memory_barrier();
}

static int spi_wait_fifo_space(struct airlink_display_status *status,
                               uint32_t *used)
{
    uint32_t timeout = 5000000U;

    for (;;) {
        *used = mmio_read(SPI_TXFLR);
        if (*used > status->max_txflr)
            status->max_txflr = *used;
        if (*used < status->fifo_depth)
            return 0;
        if (--timeout == 0U) {
            set_error(status, AIRLINK_DISPLAY_ERROR_SPI_TIMEOUT);
            return -1;
        }
    }
}

static int spi_wait_idle(struct airlink_display_status *status)
{
    uint32_t timeout = 10000000U;

    for (;;) {
        uint32_t txflr = mmio_read(SPI_TXFLR);
        uint32_t sr = mmio_read(SPI_SR);

        if (txflr > status->max_txflr)
            status->max_txflr = txflr;
        if (txflr == 0U && (sr & SPI_SR_TF_EMPTY) != 0U &&
            (sr & SPI_SR_BUSY) == 0U)
            return 0;
        if (--timeout == 0U) {
            set_error(status, AIRLINK_DISPLAY_ERROR_SPI_TIMEOUT);
            return -1;
        }
    }
}

static void spi_drain_rx(void)
{
    while (mmio_read(SPI_RXFLR) != 0U)
        (void)mmio_read(SPI_DR);
}

static int spi_transfer(struct airlink_display_status *status,
                        const uint8_t *data, size_t length)
{
    size_t index = 0U;

    mmio_write(SPI_SER, 1U);
    while (index < length) {
        uint32_t used;
        uint32_t room;

        if (spi_wait_fifo_space(status, &used) != 0) {
            mmio_write(SPI_SER, 0U);
            return -1;
        }
        room = status->fifo_depth - used;
        while (room-- != 0U && index < length)
            mmio_write(SPI_DR, data[index++]);
    }
    if (spi_wait_idle(status) != 0) {
        mmio_write(SPI_SER, 0U);
        return -1;
    }
    mmio_write(SPI_SER, 0U);
    spi_drain_rx();
    return 0;
}

static int panel_command_write(struct airlink_display_status *status,
                               uint8_t command, const uint8_t *data,
                               size_t length)
{
    gpio_set(LCD_DC_MASK, 0);
    if (spi_transfer(status, &command, 1U) != 0)
        return -1;
    if (length != 0U) {
        gpio_set(LCD_DC_MASK, 1);
        if (spi_transfer(status, data, length) != 0)
            return -1;
    }
    return 0;
}

static int panel_prepare_controller(struct airlink_display_status *status)
{
    for (size_t index = 0U;
         index < sizeof(panel_init) / sizeof(panel_init[0]); ++index) {
        if (panel_command_write(status, panel_init[index].command,
                                panel_init[index].data,
                                panel_init[index].length) != 0)
            return -1;
    }

    if (panel_command_write(status, 0x11U, NULL, 0U) != 0)
        return -1;
    delay_ms(120U);
    return 0;
}

static int panel_enable_display(struct airlink_display_status *status)
{
    return panel_command_write(status, 0x29U, NULL, 0U);
}

static int panel_set_window(struct airlink_display_status *status,
                            uint32_t x1, uint32_t y1,
                            uint32_t x2, uint32_t y2)
{
    uint8_t columns[] = {
        (uint8_t)(x1 >> 8), (uint8_t)x1,
        (uint8_t)(x2 >> 8), (uint8_t)x2,
    };
    uint8_t rows[] = {
        (uint8_t)(y1 >> 8), (uint8_t)y1,
        (uint8_t)(y2 >> 8), (uint8_t)y2,
    };

    if (panel_command_write(status, 0x2aU, columns, sizeof(columns)) != 0)
        return -1;
    if (panel_command_write(status, 0x2bU, rows, sizeof(rows)) != 0)
        return -1;
    return panel_command_write(status, 0x2cU, NULL, 0U);
}

static int panel_fill_rect(struct airlink_display_status *status,
                           uint32_t x1, uint32_t y1,
                           uint32_t x2, uint32_t y2,
                           uint16_t colour)
{
    uint8_t pair[2] = {(uint8_t)(colour >> 8), (uint8_t)colour};
    uint32_t byte_index = 0U;
    uint32_t byte_count;
    uint64_t start_cycles = read_cycle();
    uint64_t elapsed;

    if (x1 >= PANEL_SIZE || y1 >= PANEL_SIZE)
        return 0;
    if (x2 >= PANEL_SIZE)
        x2 = PANEL_SIZE - 1U;
    if (y2 >= PANEL_SIZE)
        y2 = PANEL_SIZE - 1U;
    if (x2 < x1 || y2 < y1)
        return 0;
    byte_count = (x2 - x1 + 1U) * (y2 - y1 + 1U) * 2U;

    status->max_txflr = 0U;
    if (panel_set_window(status, x1, y1, x2, y2) != 0)
        return -1;

    gpio_set(LCD_DC_MASK, 1);
    mmio_write(SPI_SER, 1U);
    while (byte_index < byte_count) {
        uint32_t used;
        uint32_t room;

        if (spi_wait_fifo_space(status, &used) != 0) {
            mmio_write(SPI_SER, 0U);
            return -1;
        }
        room = status->fifo_depth - used;
        while (room-- != 0U && byte_index < byte_count) {
            mmio_write(SPI_DR, pair[byte_index & 1U]);
            byte_index++;
        }
    }
    if (spi_wait_idle(status) != 0) {
        mmio_write(SPI_SER, 0U);
        return -1;
    }
    mmio_write(SPI_SER, 0U);
    spi_drain_rx();

    elapsed = read_cycle() - start_cycles;
    status->frame_count++;
    status->last_colour = colour;
    status->last_frame_cycles_low = (uint32_t)elapsed;
    status->last_frame_cycles_high = (uint32_t)(elapsed >> 32);
    return 0;
}

static int panel_fill(struct airlink_display_status *status, uint16_t colour)
{
    return panel_fill_rect(status, 0U, 0U, PANEL_SIZE - 1U,
                           PANEL_SIZE - 1U, colour);
}

int airlink_display_flush_rgb565(struct airlink_display_status *status,
                                 uint32_t x1, uint32_t y1,
                                 uint32_t x2, uint32_t y2,
                                 const uint16_t *pixels)
{
    uint32_t pixel_count;
    uint32_t byte_count;
    uint64_t started;
    uint64_t elapsed;

    if (!status->ready || pixels == NULL || x1 >= PANEL_SIZE || y1 >= PANEL_SIZE)
        return -1;
    if (x2 >= PANEL_SIZE) x2 = PANEL_SIZE - 1U;
    if (y2 >= PANEL_SIZE) y2 = PANEL_SIZE - 1U;
    if (x2 < x1 || y2 < y1) return 0;
    pixel_count = (x2 - x1 + 1U) * (y2 - y1 + 1U);
    byte_count = pixel_count * 2U;
    started = read_cycle();
    status->max_txflr = 0U;
    if (panel_set_window(status, x1, y1, x2, y2) != 0)
        return -1;
    gpio_set(LCD_DC_MASK, 1);
    if (spi_transfer(status, (const uint8_t *)pixels, byte_count) != 0)
        return -1;
    elapsed = read_cycle() - started;
    status->flush_count++;
    status->flush_bytes += byte_count;
    status->current_frame_bytes += byte_count;
    status->flush_total_cycles += elapsed;
    if (elapsed > status->flush_max_cycles)
        status->flush_max_cycles = (uint32_t)elapsed;
    return 0;
}

void airlink_display_complete_frame(struct airlink_display_status *status)
{
    if (status == NULL)
        return;
    status->completed_frame_count++;
    status->frame_count = status->completed_frame_count;
    status->last_frame_bytes = status->current_frame_bytes;
    status->current_frame_bytes = 0U;
}

int airlink_display_init(struct airlink_display_status *status)
{
    uint64_t start_time = read_time();
    panel_start_time = start_time;

    status->ready = 0U;
    status->error_flags = 0U;
    status->init_stage = 1U;
    status->frame_count = 0U;
    status->last_colour = 0U;
    status->spi_diagnostic = 0U;
    status->fifo_depth = 0U;
    status->max_txflr = 0U;
    status->last_frame_cycles_low = 0U;
    status->last_frame_cycles_high = 0U;
    status->clock_div_register = 0U;
    status->clock_bypass_register = 0U;
    status->reset_done_ms = 0U;
    status->sleep_out_done_ms = 0U;
    status->gram_ready_ms = 0U;
    status->visible_ms = 0U;
    status->ready_ms = 0U;
    status->visible = 0U;
    status->flush_count = 0U;
    status->flush_bytes = 0U;
    status->flush_max_cycles = 0U;
    status->flush_total_cycles = 0U;
    status->completed_frame_count = 0U;
    status->current_frame_bytes = 0U;
    status->last_frame_bytes = 0U;
    status->spi_parent_hz = 0U;
    status->spi_sclk_hz = 0U;
    status->spi_baudr = 0U;
    status->spi_high_speed = 0U;
    status->clock_bypass_register_1 = 0U;

    clock_init(status);
    pinmux_init();
    gpio_prepare_output(LCD_DC_MASK | LCD_RESET_MASK, 1);
    status->init_stage = 2U;

    gpio_set(LCD_RESET_MASK, 1);
    delay_ms(5U);
    gpio_set(LCD_RESET_MASK, 0);
    delay_ms(10U);
    gpio_set(LCD_RESET_MASK, 1);
    delay_ms(120U);
    status->reset_done_ms = elapsed_ms(start_time);
    status->init_stage = 3U;

    spi_init(status);
    status->init_stage = 4U;
    if (panel_prepare_controller(status) != 0) {
        status->init_stage = 0xe1U;
        return -1;
    }
    status->sleep_out_done_ms = elapsed_ms(start_time);
    status->init_stage = 5U;

    /* Preload a dark frame; LVGL replaces it before Display On. */
    if (panel_fill(status, 0x0842U) != 0) {
        status->init_stage = 0xe2U;
        return -1;
    }
    status->gram_ready_ms = elapsed_ms(start_time);
    status->ready = 1U;
    status->init_stage = 6U;
    return 0;
}

int airlink_display_make_visible(struct airlink_display_status *status)
{
    if (!status->ready)
        return -1;
    if (status->visible)
        return 0;
    status->gram_ready_ms = elapsed_ms(panel_start_time);
    if (panel_enable_display(status) != 0) {
        status->init_stage = 0xe3U;
        return -1;
    }
    status->visible_ms = elapsed_ms(panel_start_time);
    delay_ms(50U);
    status->visible = 1U;
    status->ready_ms = elapsed_ms(panel_start_time);
    status->init_stage = 7U;
    return 0;
}

void airlink_display_check_ownership(struct airlink_display_status *status)
{
    uint32_t expected_clock_div = CLOCK_DIV_SAFE;
    uint32_t clock_div = (mmio_read(CLOCK_SPI_DIV) & CLOCK_DIV_MASK) >>
        CLOCK_DIV_SHIFT;
    uint32_t baudr = mmio_read(SPI_BAUDR);

    if (mmio_read(PINMUX_MIPI_TXM2) != PINMUX_SPI_FUNCTION ||
        mmio_read(PINMUX_MIPI_TXP2) != PINMUX_SPI_FUNCTION ||
        mmio_read(PINMUX_MIPI_TXM1) != PINMUX_SPI_FUNCTION ||
        mmio_read(PINMUX_MIPI_TXP1) != PINMUX_SPI_FUNCTION ||
        mmio_read(PINMUX_GPIOA15) != PINMUX_GPIO_FUNCTION ||
        mmio_read(PINMUX_GPIOA27) != PINMUX_GPIO_FUNCTION) {
        set_error(status, AIRLINK_DISPLAY_ERROR_OWNER_CHANGED);
    }
    if (clock_div != expected_clock_div || baudr != status->spi_baudr) {
        set_error(status, AIRLINK_DISPLAY_ERROR_OWNER_CHANGED);
        mmio_write(SPI_SSIENR, 0U);
        if (clock_set_divider(status, expected_clock_div) != 0)
            clock_use_safe(status);
        mmio_write(SPI_BAUDR, status->spi_baudr);
        mmio_write(SPI_SSIENR, 1U);
        memory_barrier();
        status->spi_sclk_hz = status->spi_parent_hz / status->spi_baudr;
    }
}

const char *airlink_display_colour_name(uint32_t colour)
{
    switch (colour & 0xffffU) {
    case 0xffffU:
        return "WHITE";
    case 0x0000U:
        return "BLACK";
    case 0xf800U:
        return "RED";
    case 0x07e0U:
        return "GREEN";
    case 0x001fU:
        return "BLUE";
    case 0x07ffU:
        return "CYAN";
    default:
        return "UNKNOWN";
    }
}
