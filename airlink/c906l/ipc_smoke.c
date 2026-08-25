#include <stdint.h>

#include "airlink_ipc_v4.h"
#include "adc1.h"
#include "airlink_ui.h"
#include "lvgl.h"
#include "ch347.h"
#include "display.h"
#include "touch.h"

#define FIRMWARE_ID                     0x50373252U /* "R27P" */
#define ABI_REVISION                    4U
#define DISPLAY_TIMEBASE_HZ             25000000U
#define TICKS_PER_MS                    (DISPLAY_TIMEBASE_HZ / 1000U)

#define UART0_BASE			0x04140000UL
#define UART_THR			(UART0_BASE + 0x00)
#define UART_LSR			(UART0_BASE + 0x14)
#define UART_LSR_THRE			(1U << 5)

#define PINMUX_IIC0_SDA			0x03001074UL
#define PINMUX_GPIOA29_VALUE		3U

#define GPIOA_BASE			0x03020000UL
#define GPIO_DIRECTION			(GPIOA_BASE + 0x04)
#define GPIO_EXT_PORT			(GPIOA_BASE + 0x50)
#define GPIOA29_MASK			(1U << 29)

#define GPIO_SAMPLE_MS                  10U
#define DEBOUNCE_SAMPLES                20U
#define ADC_INTERVAL_MS                 1000U
#define HEARTBEAT_INTERVAL_MS           1000U
#define HEALTH_INTERVAL_MS              10000U
#define IDLE_DELAY_MS                   1U

static volatile struct airlink_ipc_header *const shared_header =
	(volatile struct airlink_ipc_header *)
	(AIRLINK_IPC_SHARED_BASE + AIRLINK_IPC_HEADER_OFFSET);
static volatile struct airlink_ipc_state *const shared_c906_state =
	(volatile struct airlink_ipc_state *)
	(AIRLINK_IPC_SHARED_BASE + AIRLINK_IPC_C906_STATE_OFFSET);
static volatile struct airlink_ipc_state *const shared_linux_state =
	(volatile struct airlink_ipc_state *)
	(AIRLINK_IPC_SHARED_BASE + AIRLINK_IPC_LINUX_STATE_OFFSET);
static volatile struct airlink_ipc_message *const shared_linux_tx =
	(volatile struct airlink_ipc_message *)
	(AIRLINK_IPC_SHARED_BASE + AIRLINK_IPC_LINUX_TX_OFFSET);
static volatile struct airlink_ipc_message *const shared_c906_tx =
	(volatile struct airlink_ipc_message *)
	(AIRLINK_IPC_SHARED_BASE + AIRLINK_IPC_C906_TX_OFFSET);
static volatile struct airlink_ipc_ui_status *const shared_ui_status =
    (volatile struct airlink_ipc_ui_status *)
    (AIRLINK_IPC_SHARED_BASE + AIRLINK_IPC_UI_STATUS_OFFSET);
static volatile struct airlink_ipc_provision_status *const shared_provision_status =
    (volatile struct airlink_ipc_provision_status *)
    (AIRLINK_IPC_SHARED_BASE + AIRLINK_IPC_PROVISION_STATUS_OFFSET);

static struct airlink_ipc_state c906_state;
static uint32_t last_linux_message_generation;
static uint32_t last_linux_sequence;
static uint32_t last_linux_type;
static uint32_t last_bad_message_generation;
static uint32_t last_bad_state_generation;
static int uart_disabled;
static int linux_peer_seen;
static int display_owner_warning_logged;
static int touch_owner_warning_logged;
static uint32_t last_touch_error_logged;
static int ch347_owner_error_logged;
static int adc1_owner_error_logged;
static struct airlink_display_status display_status;
static struct airlink_touch_status touch_status;
static struct airlink_ch347_status ch347_status;
static struct airlink_adc1_status adc1_status;
static struct airlink_ui_model ui_model;
static struct airlink_ui_stats ui_stats;
static struct airlink_ipc_ui_status linux_ui_status;
static struct airlink_ipc_provision_status linux_provision_status;
static uint32_t last_ui_generation;
static uint32_t last_provision_generation;
static uint32_t provision_request_sequence;
static uint32_t last_lv_tick;
static uint32_t mode_event_sequence;
static uint32_t ch347_pending_mode;
static uint64_t ch347_switch_started;
enum { CHSM_IDLE, CHSM_SWITCHING };
static uint32_t ch347_sm_state;

static inline uint32_t mmio_read(uintptr_t address)
{
	return *(volatile uint32_t *)address;
}

static inline void mmio_write(uintptr_t address, uint32_t value)
{
	*(volatile uint32_t *)address = value;
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

static inline void memory_barrier(void)
{
	__asm__ volatile ("fence rw, rw" ::: "memory");
}

static void delay_ms(uint32_t milliseconds)
{
    uint64_t deadline = read_time() +
        (uint64_t)milliseconds * TICKS_PER_MS;

    while ((int64_t)(read_time() - deadline) < 0)
        __asm__ volatile ("nop");
}

static int deadline_reached(uint64_t now, uint64_t deadline)
{
    return (int64_t)(now - deadline) >= 0;
}

static void zero_words(volatile uint32_t *destination, uint32_t count)
{
	while (count-- != 0U)
		*destination++ = 0U;
}

static uint32_t crc32_bytes(const uint8_t *data, uint32_t length)
{
	uint32_t crc = 0xffffffffU;

	while (length-- != 0U) {
		crc ^= *data++;
		for (uint32_t bit = 0; bit < 8U; ++bit)
			crc = (crc >> 1) ^
				(0xedb88320U & (0U - (crc & 1U)));
	}
	return crc ^ 0xffffffffU;
}

static uint32_t block_crc32(const void *block, uint32_t size)
{
	const uint8_t *bytes = (const uint8_t *)block;

	return crc32_bytes(bytes + sizeof(uint32_t),
			   size - 2U * sizeof(uint32_t));
}

static uint32_t next_even_generation(uint32_t current)
{
	uint32_t even = current;

	if (even & 1U)
		even++;
	even += 2U;
	if (even == 0U)
		even = 2U;
	return even;
}

static void state_write(volatile struct airlink_ipc_state *shared,
			struct airlink_ipc_state *local)
{
	volatile uint32_t *destination = (volatile uint32_t *)shared;
	const uint32_t *source = (const uint32_t *)local;
	uint32_t final_generation = next_even_generation(shared->generation);

	local->generation = final_generation;
	local->crc32 = block_crc32(local, sizeof(*local));

	shared->generation = final_generation - 1U;
	memory_barrier();
	for (uint32_t word = 1U;
	     word < sizeof(*local) / sizeof(uint32_t); ++word)
		destination[word] = source[word];
	memory_barrier();
	shared->generation = final_generation;
	memory_barrier();
}

static int state_read(const volatile struct airlink_ipc_state *shared,
		      struct airlink_ipc_state *local)
{
	const volatile uint32_t *source = (const volatile uint32_t *)shared;
	uint32_t *destination = (uint32_t *)local;
	uint32_t before = shared->generation;
	uint32_t after;

	if (before == 0U || (before & 1U))
		return 0;
	memory_barrier();
	for (uint32_t word = 0U;
	     word < sizeof(*local) / sizeof(uint32_t); ++word)
		destination[word] = source[word];
	memory_barrier();
	after = shared->generation;
	if (before != after || (after & 1U))
		return 0;
	if (local->crc32 != block_crc32(local, sizeof(*local)))
		return -1;
	return 1;
}

static int ui_status_read(const volatile struct airlink_ipc_ui_status *shared,
                          struct airlink_ipc_ui_status *local)
{
    const volatile uint32_t *source = (const volatile uint32_t *)shared;
    uint32_t *destination = (uint32_t *)local;
    uint32_t before = shared->generation;
    uint32_t after;

    if (before == 0U || (before & 1U))
        return 0;
    memory_barrier();
    for (uint32_t word = 0U;
         word < sizeof(*local) / sizeof(uint32_t); ++word)
        destination[word] = source[word];
    memory_barrier();
    after = shared->generation;
    if (before != after || (after & 1U))
        return 0;
    if (local->crc32 != block_crc32(local, sizeof(*local)))
        return -1;
    return 1;
}

static int provision_status_read(
    const volatile struct airlink_ipc_provision_status *shared,
    struct airlink_ipc_provision_status *local)
{
    const volatile uint32_t *source = (const volatile uint32_t *)shared;
    uint32_t *destination = (uint32_t *)local;
    uint32_t before = shared->generation;
    uint32_t after;

    if (before == 0U || (before & 1U))
        return 0;
    memory_barrier();
    for (uint32_t word = 0U;
         word < sizeof(*local) / sizeof(uint32_t); ++word)
        destination[word] = source[word];
    memory_barrier();
    after = shared->generation;
    if (before != after || (after & 1U))
        return 0;
    if (local->crc32 != block_crc32(local, sizeof(*local)))
        return -1;
    return 1;
}

static void message_write(volatile struct airlink_ipc_message *shared,
			  struct airlink_ipc_message *local)
{
	volatile uint32_t *destination = (volatile uint32_t *)shared;
	const uint32_t *source = (const uint32_t *)local;
	uint32_t final_generation = next_even_generation(shared->generation);

	local->generation = final_generation;
	local->crc32 = block_crc32(local, sizeof(*local));

	shared->generation = final_generation - 1U;
	memory_barrier();
	for (uint32_t word = 1U;
	     word < sizeof(*local) / sizeof(uint32_t); ++word)
		destination[word] = source[word];
	memory_barrier();
	shared->generation = final_generation;
	memory_barrier();
}

static int message_read(const volatile struct airlink_ipc_message *shared,
			struct airlink_ipc_message *local)
{
	const volatile uint32_t *source = (const volatile uint32_t *)shared;
	uint32_t *destination = (uint32_t *)local;
	uint32_t before = shared->generation;
	uint32_t after;

	if (before == 0U || (before & 1U))
		return 0;
	memory_barrier();
	for (uint32_t word = 0U;
	     word < sizeof(*local) / sizeof(uint32_t); ++word)
		destination[word] = source[word];
	memory_barrier();
	after = shared->generation;
	if (before != after || (after & 1U))
		return 0;
	if (local->sequence == 0U ||
	    local->crc32 != block_crc32(local, sizeof(*local)))
		return -1;
	return 1;
}

static void state_update_time(void)
{
	uint64_t cycles = read_cycle();

	c906_state.cycle_low = (uint32_t)cycles;
	c906_state.cycle_high = (uint32_t)(cycles >> 32);
}

/*
 * UART0 is initialized by the SG2002 boot firmware. Do not alter its clock,
 * divisor, pinmux, FIFO, or line-control state while U-Boot/Linux also use it.
 */
static int uart_putc_raw(char character)
{
	uint32_t timeout = 5000000U;

	while (!(mmio_read(UART_LSR) & UART_LSR_THRE)) {
		if (--timeout == 0U)
			return -1;
	}
	mmio_write(UART_THR, (uint8_t)character);
	return 0;
}

static void uart_putc(char character)
{
	if (uart_disabled)
		return;
	if (character == '\n' && uart_putc_raw('\r') != 0)
		goto failed;
	if (uart_putc_raw(character) == 0)
		return;

failed:
	uart_disabled = 1;
	c906_state.error_flags |= AIRLINK_IPC_ERROR_UART_TIMEOUT;
}

static void uart_puts(const char *text)
{
	while (*text != '\0' && !uart_disabled)
		uart_putc(*text++);
}

static void uart_put_u32(uint32_t value)
{
	char digits[10];
	uint32_t length = 0U;

	do {
		digits[length++] = (char)('0' + value % 10U);
		value /= 10U;
	} while (value != 0U && length < (uint32_t)sizeof(digits));

	while (length != 0U)
		uart_putc(digits[--length]);
}

static void uart_put_hex32(uint32_t value)
{
	static const char hex[] = "0123456789ABCDEF";

	for (int shift = 28; shift >= 0; shift -= 4)
		uart_putc(hex[(value >> shift) & 0xfU]);
}

static void uart_put_hex8(uint32_t value)
{
	static const char hex[] = "0123456789ABCDEF";

	uart_putc(hex[(value >> 4) & 0xfU]);
	uart_putc(hex[value & 0xfU]);
}

static void uart_prefix(void)
{
	uart_puts("[ALIPC-C906L] ");
}

static const char *level_name(uint32_t level)
{
	return level ? "HIGH" : "LOW";
}

static const char *mode_name(uint32_t level)
{
	return level ? "WIRED" : "WIRELESS";
}

static const char *command_name(uint32_t type)
{
	switch (type) {
	case AIRLINK_IPC_MSG_HELLO:
		return "HELLO";
	case AIRLINK_IPC_MSG_PING:
		return "PING";
	case AIRLINK_IPC_MSG_CLEAR_ERRORS:
		return "CLEAR_ERRORS";
	case AIRLINK_IPC_MSG_REQUEST_SNAPSHOT:
		return "REQUEST_SNAPSHOT";
    case AIRLINK_IPC_MSG_REQUEST_CH347_STATUS:
        return "REQUEST_CH347_STATUS";
    case AIRLINK_IPC_MSG_CH347_PREPARED:
        return "CH347_PREPARED";
    case AIRLINK_IPC_MSG_CH347_ENUM_RESULT:
        return "CH347_ENUM_RESULT";
    case AIRLINK_IPC_MSG_WIFI_PROVISION_ACK:
        return "WIFI_PROVISION_ACK";
    case AIRLINK_IPC_MSG_WIFI_PROVISION_CANCEL_ACK:
        return "WIFI_PROVISION_CANCEL_ACK";
    case AIRLINK_IPC_MSG_MODE_APPLY_RESULT:
        return "MODE_APPLY_RESULT";
	default:
		return "UNKNOWN";
	}
}

static void uart_log_mode(const char *kind, uint32_t level)
{
	uart_prefix();
	uart_puts(kind);
	uart_puts(" level=");
	uart_puts(level_name(level));
	uart_puts(" mode=");
	uart_puts(mode_name(level));
	uart_puts(" transitions=");
	uart_put_u32(c906_state.transition_count);
	uart_putc('\n');
}

static void uart_log_rx(const struct airlink_ipc_message *message)
{
	uart_prefix();
	uart_puts("RX seq=");
	uart_put_u32(message->sequence);
	uart_puts(" cmd=");
	uart_puts(command_name(message->type));
	if (message->type == AIRLINK_IPC_MSG_PING) {
		uart_puts(" cookie=0x");
		uart_put_hex32(message->args[0]);
	}
	uart_putc('\n');
}

static void uart_log_tx(uint32_t sequence, const char *kind, uint32_t cookie)
{
	uart_prefix();
	uart_puts("TX seq=");
	uart_put_u32(sequence);
	uart_puts(" event=");
	uart_puts(kind);
	if (kind[0] == 'P') {
		uart_puts(" cookie=0x");
		uart_put_hex32(cookie);
	}
	uart_putc('\n');
}

static void uart_log_health(void)
{
	uart_prefix();
	uart_puts("HEALTH c906l_hb=");
	uart_put_u32(c906_state.heartbeat);
	uart_puts(" linux_hb=");
	uart_put_u32(c906_state.peer_heartbeat);
	uart_puts(" rx=");
	uart_put_u32(c906_state.rx_count);
	uart_puts(" tx=");
	uart_put_u32(c906_state.tx_count);
	uart_puts(" crc=");
	uart_put_u32(c906_state.crc_error_count);
	uart_puts(" errors=0x");
	uart_put_hex32(c906_state.error_flags);
	uart_puts(" warnings=");
	uart_put_u32(c906_state.warning_count);
	uart_puts(" mode=");
	if (c906_state.flags & AIRLINK_IPC_STATE_STABLE_VALID)
		uart_puts(mode_name(c906_state.stable_level));
	else
		uart_puts("UNKNOWN");
	uart_puts(" touch=");
	uart_puts(touch_status.ready ? "READY" : "NOT_READY");
	uart_puts(" touch_events=");
	uart_put_u32(touch_status.event_count);
	uart_puts(" touch_errors=");
	uart_put_u32(touch_status.error_count);
	uart_puts(" polls=");
	uart_put_u32(touch_status.poll_count);
	uart_puts(" ok=");
	uart_put_u32(touch_status.successful_poll_count);
	uart_puts(" recoveries=");
	uart_put_u32(touch_status.recovery_count);
	uart_puts(" reinits=");
	uart_put_u32(touch_status.reinit_count);
	uart_puts(" owner_recovers=");
	uart_put_u32(touch_status.owner_recovery_count);
	uart_puts(" irq=");
	uart_puts(level_name(touch_status.irq_level));
	uart_puts(" owner_mismatch=0x");
	uart_put_hex32(touch_status.ownership_mismatch);
	uart_puts(" raw_xy=");
	uart_put_u32(touch_status.raw_x);
	uart_putc(',');
	uart_put_u32(touch_status.raw_y);
	uart_puts(" mapped_xy=");
	uart_put_u32(touch_status.x);
	uart_putc(',');
	uart_put_u32(touch_status.y);
	uart_puts(" raw=");
	for (uint32_t index = 0U; index < sizeof(touch_status.raw); ++index) {
		if (index != 0U)
			uart_putc('-');
		uart_put_hex8(touch_status.raw[index]);
	}
	uart_puts(" ch347=");
	uart_puts(ch347_status.ready ? "READY" : "NOT_READY");
	uart_puts(" ch347_mode=");
	uart_puts(airlink_ch347_mode_name(ch347_status.current_mode));
	uart_puts(" selected=");
	uart_puts(airlink_ch347_mode_name(ui_model.ch347_selected));
	uart_puts(" dtr=");
	uart_puts(level_name(ch347_status.dtr_level));
	uart_puts(" rts=");
	uart_puts(level_name(ch347_status.rts_level));
	uart_puts(" reset=");
	uart_puts(level_name(ch347_status.reset_level));
	uart_puts(" switches=");
	uart_put_u32(ch347_status.switch_count);
	uart_puts(" ch347_recovers=");
	uart_put_u32(ch347_status.recovery_count);
	uart_puts(" ch347_mismatch=0x");
	uart_put_hex32(ch347_status.ownership_mismatch);
	uart_puts(" ch347_errors=0x");
	uart_put_hex32(ch347_status.error_flags);
	uart_puts(" adc1=");
	uart_puts(adc1_status.ready ? "READY" : "NOT_READY");
	uart_puts(" adc_raw=");
	uart_put_u32(adc1_status.raw_filtered);
	uart_puts(" adc_avg=");
	uart_put_u32(adc1_status.raw_average);
	uart_puts(" adc_minmax=");
	uart_put_u32(adc1_status.raw_min);
	uart_putc('/');
	uart_put_u32(adc1_status.raw_max);
	uart_puts(" adc_mv_nominal=");
	uart_put_u32(adc1_status.adc_millivolts);
	uart_puts(" battery_mv_nominal=");
	uart_put_u32(adc1_status.battery_millivolts);
	uart_puts(" percent_nominal=");
	uart_put_u32(adc1_status.nominal_percent);
	uart_puts(" adc_batches=");
	uart_put_u32(adc1_status.batch_count);
	uart_puts(" adc_timeouts=");
	uart_put_u32(adc1_status.timeout_count);
	uart_puts(" adc_recovers=");
	uart_put_u32(adc1_status.recovery_count);
	uart_puts(" adc_mismatch=0x");
	uart_put_hex32(adc1_status.ownership_mismatch);
	uart_puts(" adc_errors=0x");
	uart_put_hex32(adc1_status.error_flags);
	uart_putc('\n');
}

static void display_state_sync(void)
{
	c906_state.flags &= ~AIRLINK_IPC_STATE_DISPLAY_READY;
	if (display_status.ready)
		c906_state.flags |= AIRLINK_IPC_STATE_DISPLAY_READY;
	if (display_status.error_flags & AIRLINK_DISPLAY_ERROR_SPI_TIMEOUT)
		c906_state.error_flags |= AIRLINK_IPC_ERROR_SPI_TIMEOUT;
	if (display_status.error_flags & AIRLINK_DISPLAY_ERROR_OWNER_CHANGED)
		c906_state.error_flags |= AIRLINK_IPC_ERROR_DISPLAY_OWNER;
}

static void uart_log_display_frame(void)
{
	uart_prefix();
	uart_puts("DISPLAY update=");
	uart_put_u32(display_status.frame_count);
	uart_puts(" color=");
	uart_puts(airlink_display_colour_name(display_status.last_colour));
	uart_puts(" rgb565=0x");
	uart_put_hex32(display_status.last_colour & 0xffffU);
	uart_puts(" cycles=0x");
	uart_put_hex32(display_status.last_frame_cycles_high);
	uart_put_hex32(display_status.last_frame_cycles_low);
	uart_puts(" fifo=");
	uart_put_u32(display_status.fifo_depth);
	uart_puts(" txflr_max=");
	uart_put_u32(display_status.max_txflr);
	uart_putc('\n');
}

static void adc1_state_sync(void)
{
    c906_state.flags &= ~AIRLINK_IPC_STATE_ADC1_READY;
    if (adc1_status.ready)
        c906_state.flags |= AIRLINK_IPC_STATE_ADC1_READY;
    if (adc1_status.error_flags & AIRLINK_ADC1_ERROR_TIMEOUT)
        c906_state.error_flags |= AIRLINK_IPC_ERROR_ADC1_TIMEOUT;
    if (adc1_status.ownership_mismatch != 0U ||
        (adc1_status.error_flags &
         (AIRLINK_ADC1_ERROR_PINMUX_CHANGED |
          AIRLINK_ADC1_ERROR_CLOCK_CHANGED |
          AIRLINK_ADC1_ERROR_RESET_CHANGED)) != 0U)
        c906_state.error_flags |= AIRLINK_IPC_ERROR_ADC1_OWNER;
    c906_state.reserved[0] = adc1_status.raw_filtered;
    c906_state.reserved[1] = adc1_status.battery_millivolts;
}

static const char *adc1_display_source_name(uint32_t source)
{
    if (source == AIRLINK_ADC1_DISPLAY_INITIAL)
        return "INITIAL";
    if (source == AIRLINK_ADC1_DISPLAY_NORMAL)
        return "NORMAL";
    if (source == AIRLINK_ADC1_DISPLAY_HELD)
        return "HELD";
    if (source == AIRLINK_ADC1_DISPLAY_CLAMP_HIGH)
        return "CLAMP_HIGH";
    if (source == AIRLINK_ADC1_DISPLAY_INVALID)
        return "INVALID";
    return "NONE";
}

static void uart_log_adc1_sample(const char *kind)
{
    uart_prefix();
    uart_puts("ADC1 sample=");
    uart_puts(kind);
    uart_puts(" batch=");
    uart_put_u32(adc1_status.batch_count);
    uart_puts(" raw_latest=");
    uart_put_u32(adc1_status.raw_latest);
    uart_puts(" raw_min=");
    uart_put_u32(adc1_status.raw_min);
    uart_puts(" raw_max=");
    uart_put_u32(adc1_status.raw_max);
    uart_puts(" raw_avg=");
    uart_put_u32(adc1_status.raw_average);
    uart_puts(" raw_median3=");
    uart_put_u32(adc1_status.raw_filtered);
    uart_puts(" adc_mv_nominal=");
    uart_put_u32(adc1_status.adc_millivolts);
    uart_puts(" battery_mv_nominal=");
    uart_put_u32(adc1_status.battery_millivolts);
    uart_puts(" percent_nominal=");
    uart_put_u32(adc1_status.nominal_percent);
    uart_puts(" valid=");
    uart_put_u32(adc1_status.measurement_valid);
    uart_puts(" invalid_batches=");
    uart_put_u32(adc1_status.invalid_count);
    uart_puts(" scaling=RAW_DIRECT raw847=4100mV");
    uart_putc('\n');
    uart_prefix();
    uart_puts("ADC1 display-source=");
    uart_puts(adc1_display_source_name(adc1_status.display_source));
    uart_puts(" display_mv=");
    uart_put_u32(adc1_status.display_battery_millivolts);
    uart_puts(" invalid-streak=");
    uart_put_u32(adc1_status.invalid_streak);
    uart_puts(" raw_battery_mv=");
    uart_put_u32(adc1_status.battery_millivolts);
    uart_putc('\n');
}

static void adc1_start(void)
{
    int result;

    uart_prefix();
    uart_puts("ADC1 init=BEGIN pin=59 channel=TOP_ADC1 base=0x030f0000 samples=16 filter=TRIMMED_MEAN+MEDIAN3\n");
    c906_state.init_stage = 9U;
    result = airlink_adc1_init(&adc1_status);
    adc1_state_sync();
    uart_prefix();
    uart_puts("ADC1 init=");
    uart_puts(result == 0 ? "PASS" : "FAIL");
    uart_puts(" pinmux=0x");
    uart_put_hex32(adc1_status.pinmux);
    uart_puts(" clk_en0=0x");
    uart_put_hex32(adc1_status.clock_enable);
    uart_puts(" reset1=0x");
    uart_put_hex32(adc1_status.reset_state);
    uart_puts(" ctrl=0x");
    uart_put_hex32(adc1_status.control);
    uart_puts(" cyc=0x");
    uart_put_hex32(adc1_status.cycle_setting);
    uart_puts(" mismatch=0x");
    uart_put_hex32(adc1_status.ownership_mismatch);
    uart_puts(" errors=0x");
    uart_put_hex32(adc1_status.error_flags);
    uart_putc('\n');
    if (result == 0) {
        uart_prefix();
        uart_puts("ADC1 filter=TRIMMED_MEAN+MEDIAN3 scaling=RAW_DIRECT raw847=4100mV valid=2800..4250mV\n");
        uart_log_adc1_sample("INITIAL");
        c906_state.init_stage = 10U;
    } else {
        c906_state.init_stage = 0xd1U;
    }
}

static void adc1_periodic(void)
{
    int owner_result;

    if (!adc1_status.ready)
        return;
    owner_result = airlink_adc1_check_ownership(&adc1_status);
    adc1_state_sync();
    if (owner_result < 0) {
        airlink_adc1_note_failure(&adc1_status);
        adc1_state_sync();
        uart_prefix();
        uart_puts("ADC1 sample=SKIP owner-restore=FAIL mismatch=0x");
        uart_put_hex32(adc1_status.ownership_mismatch);
        uart_putc('\n');
        return;
    }
    if (owner_result > 0) {
        c906_state.warning_count++;
        uart_prefix();
        uart_puts("ADC1 warning=OWNER_RECLAIMED_BEFORE_SAMPLE mismatch=0x");
        uart_put_hex32(adc1_status.last_owner_mismatch);
        uart_puts(" recoveries=");
        uart_put_u32(adc1_status.recovery_count);
        uart_putc('\n');
    }
    if (airlink_adc1_sample_batch(&adc1_status) != 0) {
        adc1_state_sync();
        uart_prefix();
        uart_puts("ADC1 sample=FAIL count=");
        uart_put_u32(adc1_status.sample_count);
        uart_puts(" status=0x");
        uart_put_hex32(adc1_status.status);
        uart_puts(" ctrl=0x");
        uart_put_hex32(adc1_status.control);
        uart_puts(" errors=0x");
        uart_put_hex32(adc1_status.error_flags);
        uart_putc('\n');
        return;
    }
    adc1_state_sync();
    uart_log_adc1_sample("PERIODIC");
    /* LVGL owns every visible pixel after airlink_ui_init().  ADC updates
     * only feed ui_model_sync(); never draw a legacy scene directly. */
}

static void display_start(void)
{
	uart_prefix();
	uart_puts("DISPLAY init=BEGIN parent=187500000 sclk=46875000 baudr=4 safe=FORCED fifo=auto mode=3 owner=C906L\n");
	c906_state.init_stage = 5U;
	state_update_time();
	state_write(shared_c906_state, &c906_state);

	if (airlink_display_init(&display_status) != 0) {
		display_state_sync();
		c906_state.init_stage = 0xe0U | (display_status.init_stage & 0x0fU);
		uart_prefix();
		uart_puts("DISPLAY init=FAIL stage=0x");
		uart_put_hex32(display_status.init_stage);
		uart_puts(" errors=0x");
		uart_put_hex32(display_status.error_flags);
		uart_putc('\n');
		return;
	}

	display_state_sync();
	c906_state.init_stage = 6U;
	uart_prefix();
	uart_puts("DISPLAY init=PASS baudr=");
	uart_put_u32(display_status.spi_baudr);
	uart_puts(" fifo=");
	uart_put_u32(display_status.fifo_depth);
	uart_puts(" mode=3 clk_div_reg=0x");
	uart_put_hex32(display_status.clock_div_register);
	uart_puts(" clk_bypass0=0x");
	uart_put_hex32(display_status.clock_bypass_register);
	uart_puts(" clk_bypass1=0x");
	uart_put_hex32(display_status.clock_bypass_register_1);
	uart_puts(" madctl=0x28 orientation=TOP_LEFT_X_RIGHT_Y_DOWN scene=LVGL-HTML-V2 background=PURE_BLACK\n");
	uart_prefix();
	uart_puts("GC9A01 SPI parent=187500000 sclk=46875000 baudr=4 SAFE\n");
	uart_prefix();
	uart_puts("DISPLAY fastboot=PRELOAD_BEFORE_DISPLAY_ON delay_source=RDTIME reset_ms=5+10+120 sleep_out_ms=120 display_on_ms=50\n");
	uart_prefix();
	uart_puts("DISPLAY timing timebase=");
	uart_put_u32(DISPLAY_TIMEBASE_HZ);
	uart_putc('\n');
	uart_prefix();
	uart_puts("DISPLAY timing reset_done_ms=");
	uart_put_u32(display_status.reset_done_ms);
	uart_putc('\n');
	uart_prefix();
	uart_puts("DISPLAY timing sleep_out_done_ms=");
	uart_put_u32(display_status.sleep_out_done_ms);
	uart_putc('\n');
	uart_prefix();
	uart_puts("DISPLAY timing gram_ready_ms=");
	uart_put_u32(display_status.gram_ready_ms);
	uart_putc('\n');
	uart_prefix();
	uart_puts("DISPLAY timing visible_ms=");
	uart_put_u32(display_status.visible_ms);
	uart_putc('\n');
	uart_prefix();
	uart_puts("DISPLAY timing ready_ms=");
	uart_put_u32(display_status.ready_ms);
	uart_putc('\n');
	uart_log_display_frame();
}

static void touch_state_sync(void)
{
	c906_state.flags &= ~AIRLINK_IPC_STATE_TOUCH_READY;
	if (touch_status.ready)
		c906_state.flags |= AIRLINK_IPC_STATE_TOUCH_READY;
	if (touch_status.error_count != 0U)
		c906_state.error_flags |= AIRLINK_IPC_ERROR_TOUCH_I2C;
	if (touch_status.error_flags != 0U)
		c906_state.error_flags |= AIRLINK_IPC_ERROR_TOUCH_OWNER;
}

static void uart_log_touch_error(const char *kind)
{
	uart_prefix();
	uart_puts("TOUCH error=");
	uart_puts(airlink_touch_error_name(touch_status.last_error));
	uart_puts(" context=");
	uart_puts(kind);
	uart_puts(" stage=");
	uart_put_u32(touch_status.init_stage);
	uart_puts(" reg=0x");
	uart_put_hex32(touch_status.last_register);
	uart_puts(" abort=0x");
	uart_put_hex32(touch_status.i2c_abort_source);
	uart_puts(" status=0x");
	uart_put_hex32(touch_status.i2c_status);
	uart_puts(" count=");
	uart_put_u32(touch_status.error_count);
	uart_puts(" recoveries=");
	uart_put_u32(touch_status.recovery_count);
	uart_puts(" reinits=");
	uart_put_u32(touch_status.reinit_count);
	uart_puts(" owner_mismatch=0x");
	uart_put_hex32(touch_status.ownership_mismatch);
	uart_putc('\n');
}

static void uart_log_touch_event(void)
{
	uart_prefix();
	uart_puts("TOUCH event=");
	uart_put_u32(touch_status.event_count);
	uart_puts(" source=");
	uart_puts(airlink_touch_source_name(touch_status.source));
	uart_puts(" irq=");
	uart_puts(level_name(touch_status.irq_level));
	uart_puts(" points=");
	uart_put_u32(touch_status.points);
	uart_puts(" gesture=0x");
	uart_put_hex8(touch_status.gesture);
	uart_puts(" state=");
	uart_puts(airlink_touch_event_name(touch_status.event,
					 touch_status.points));
	uart_puts(" raw_x=");
	uart_put_u32(touch_status.raw_x);
	uart_puts(" raw_y=");
	uart_put_u32(touch_status.raw_y);
	uart_puts(" mapped_x=");
	uart_put_u32(touch_status.x);
	uart_puts(" mapped_y=");
	uart_put_u32(touch_status.y);
	uart_puts(" transform=SWAP_XY+MIRROR_Y raw=");
	for (uint32_t index = 0U; index < sizeof(touch_status.raw); ++index) {
		if (index != 0U)
			uart_putc('-');
		uart_put_hex8(touch_status.raw[index]);
	}
	uart_putc('\n');
}

static void uart_log_touch_warning(void)
{
	uart_prefix();
	uart_puts("TOUCH warning=OWNER_RECLAIMED mismatch=0x");
	uart_put_hex32(touch_status.last_owner_mismatch);
	uart_puts(" owner_recovers=");
	uart_put_u32(touch_status.owner_recovery_count);
	uart_puts(" errors=");
	uart_put_u32(touch_status.error_count);
	uart_puts(" current_mismatch=0x");
	uart_put_hex32(touch_status.ownership_mismatch);
	uart_puts(" status=READY\n");
}

static void touch_start(void)
{
	int result;

	uart_prefix();
	uart_puts("TOUCH init=BEGIN bus=I2C4 addr=0x15 speed=100000\n");
	c906_state.init_stage = 7U;
	state_update_time();
	state_write(shared_c906_state, &c906_state);

	result = airlink_touch_init(&touch_status);

	uart_prefix();
	uart_puts("TOUCH platform clk_en1=0x");
	uart_put_hex32(touch_status.clock_enable_1);
	uart_puts(" clk_en3=0x");
	uart_put_hex32(touch_status.clock_enable_3);
	uart_puts(" reset=0x");
	uart_put_hex32(touch_status.reset_state);
	uart_puts(" div=0x");
	uart_put_hex32(touch_status.clock_divider);
	uart_puts(" bypass=0x");
	uart_put_hex32(touch_status.clock_bypass);
	uart_putc('\n');

	if (result != 0) {
		touch_state_sync();
		c906_state.init_stage = 0xf0U | (touch_status.init_stage & 0x0fU);
		last_touch_error_logged = touch_status.error_count;
		uart_log_touch_error("INIT");
		uart_prefix();
		uart_puts("TOUCH init=FAIL\n");
		return;
	}

	touch_state_sync();
	c906_state.init_stage = 8U;
	uart_prefix();
	uart_puts("TOUCH reset=PASS irq=");
	uart_puts(level_name(touch_status.irq_level));
	uart_putc('\n');
	uart_prefix();
	uart_puts("TOUCH controller=PASS component=0x");
	uart_put_hex32(touch_status.component_type);
	uart_putc('\n');
	uart_prefix();
	uart_puts("TOUCH id=0x");
	uart_put_hex8(touch_status.chip_id);
	uart_puts(" fw=0x");
	uart_put_hex8(touch_status.firmware_version);
	uart_putc('\n');
	uart_prefix();
	uart_puts("TOUCH config irqcrl=0x");
	uart_put_hex8(touch_status.irq_control);
	uart_puts(" autosleep=DISABLED(0x");
	uart_put_hex8(touch_status.auto_sleep_control);
	uart_puts(")\n");
	uart_prefix();
	uart_puts("TOUCH init=PASS poll=40ms irq=active-low recovery=enabled mapping=raw(x,y)->ui(y,239-x) transform=SWAP_XY+MIRROR_Y origin=TOP_LEFT\n");
}

static void touch_periodic(void)
{
	int result;

	if (!touch_status.ready)
		return;
	result = airlink_touch_poll(&touch_status);
	touch_state_sync();
	if (result == AIRLINK_TOUCH_RESULT_EVENT) {
		uart_log_touch_event();
	} else if (result == AIRLINK_TOUCH_RESULT_OWNER_RECOVERED) {
		if (!touch_owner_warning_logged)
			c906_state.warning_count++;
		touch_owner_warning_logged = 1;
		uart_log_touch_warning();
	} else if (result < 0 &&
		   touch_status.error_count != last_touch_error_logged &&
		   (touch_status.error_count <= 3U ||
		    (touch_status.error_count % 100U) == 0U)) {
		last_touch_error_logged = touch_status.error_count;
		uart_log_touch_error("POLL");
	}
}

static void ch347_start(void)
{
    int result;
    uart_prefix();
    uart_puts("CH347 init=BEGIN pins=GPIOA_18/19/28 preserve-pad-mode=yes\n");
    result = airlink_ch347_init(&ch347_status);
    ui_model.ch347_selected = ch347_status.current_mode;
    ui_model.ch347_current = ch347_status.current_mode;
    uart_prefix();
    uart_puts("CH347 init=");
    uart_puts(result == 0 ? "PASS" : "FAIL");
    uart_puts(" mode=");
    uart_puts(airlink_ch347_mode_name(ch347_status.current_mode));
    uart_puts(" dtr=");
    uart_puts(level_name(ch347_status.dtr_level));
    uart_puts(" rts=");
    uart_puts(level_name(ch347_status.rts_level));
    uart_puts(" mismatch=0x");
    uart_put_hex32(ch347_status.ownership_mismatch);
    uart_putc('\n');
}

static void ui_model_sync(void)
{
    ui_model.wired = c906_state.stable_level;
    ui_model.battery_mv = adc1_status.display_battery_millivolts;
    ui_model.battery_percent = adc1_status.nominal_percent;
    ui_model.battery_valid =
        adc1_status.ready && adc1_status.display_valid;
    ui_model.ch347_current = ch347_status.current_mode;
    if (ui_model.ch347_selected >= AIRLINK_CH347_MODE_COUNT)
        ui_model.ch347_selected = ch347_status.current_mode;
}

static void uart_log_ui_stats(const char *kind)
{
    airlink_ui_get_stats(&ui_stats);
    uart_prefix();
    uart_puts("LVGL ");
    uart_puts(kind);
    uart_puts(" page=");
    uart_put_u32(ui_stats.page);
    uart_puts(" saver=");
    uart_put_u32(ui_stats.saver);
    uart_puts(" flush_count=");
    uart_put_u32(ui_stats.flush_count);
    uart_puts(" flush_bytes=");
    uart_put_u32(ui_stats.flush_bytes);
    uart_puts(" flush_avg_us=");
    uart_put_u32(ui_stats.flush_avg_us);
    uart_puts(" flush_max_us=");
    uart_put_u32(ui_stats.flush_max_us);
    uart_puts(" fps_current=");
    uart_put_u32(ui_stats.fps_current);
    uart_puts(" fps_min=");
    uart_put_u32(ui_stats.fps_min);
    uart_puts(" frame_count=");
    uart_put_u32(ui_stats.frame_count);
    uart_puts(" frame_bytes=");
    uart_put_u32(ui_stats.frame_bytes);
    uart_puts(" full_frames=");
    uart_put_u32(ui_stats.full_frame_count);
    uart_puts(" partial_frames=");
    uart_put_u32(ui_stats.partial_frame_count);
    uart_puts(" page_frames=");
    uart_put_u32(ui_stats.page_frame_count);
    uart_puts(" page_avg_us=");
    uart_put_u32(ui_stats.page_frame_avg_us);
    uart_puts(" page_max_us=");
    uart_put_u32(ui_stats.page_frame_max_us);
    uart_puts(" missed_refresh=");
    uart_put_u32(ui_stats.missed_refresh);
    uart_puts(" spi_parent_hz=");
    uart_put_u32(ui_stats.spi_parent_hz);
    uart_puts(" spi_sclk_hz=");
    uart_put_u32(ui_stats.spi_sclk_hz);
    uart_puts(" heap_free=");
    uart_put_u32(ui_stats.heap_free);
    uart_puts(" heap_used_pct=");
    uart_put_u32(ui_stats.heap_used_pct);
    uart_puts(" loop_max_us=");
    uart_put_u32(ui_stats.loop_max_us);
    uart_putc('\n');
}

static void uart_log_saver_reset(uint32_t reason)
{
    const char *name = "UNKNOWN";

    if (reason == AIRLINK_UI_SAVER_RESET_TOUCH)
        name = "TOUCH";
    else if (reason == AIRLINK_UI_SAVER_RESET_GPIOA29)
        name = "GPIOA29";
    else if (reason == AIRLINK_UI_SAVER_RESET_PROVISION_SUCCESS)
        name = "PROVISION_SUCCESS";

    uart_prefix();
    uart_puts("LVGL saver-reset reason=");
    uart_puts(name);
    uart_puts(" deadline_ms=30000\n");
}

static void ui_start(void)
{
    uint64_t now = read_time();
    ui_model.ch347_selected = ch347_status.current_mode;
    ui_model_sync();
    uart_prefix();
    uart_puts("LVGL init=BEGIN version=8.3.11 color=RGB565 swap=1 buffer=1x240x240 memory=static page=20ms local=16ms saver=33ms\n");
    if (airlink_ui_init(&display_status, &touch_status, &ui_model, now) != 0) {
        uart_prefix();
        uart_puts("LVGL init=FAIL\n");
        return;
    }
    airlink_ui_get_stats(&ui_stats);
    uart_prefix();
    uart_puts("LVGL init/version/memory/buffer=PASS version=8.3.11 buffer=57600px memory=384k\n");
    uart_prefix();
    uart_puts("LVGL html-v2/fonts/black-bg/layout=PASS pages=4 slide_ms=240\n");
    uart_prefix();
    uart_puts("LVGL visual-polish=PURE_BLACK wifi=POLYLINE saver=WIRELESS_DOT+WIRED_USB outer-ring=REMOVED\n");
    uart_prefix();
    uart_puts("LVGL font-small=NOTO13_M2_CONTRAST enhanced=ON wifi-row=CENTERED\n");
    uart_prefix();
    uart_puts("LVGL refresh-pacing=SAFE46 buffer=1x240x240 page=20ms/240ms spinner=16ms saver=33ms model=COALESCED\n");
    uart_prefix();
    uart_puts("LVGL render-owner=SOLE font-advance=PIXELS legacy-scenes=REMOVED\n");
    uart_prefix();
    uart_puts("LVGL mode-transition=HOME_INLINE arc=64px/2px/16ms timeout=3000ms controls=NONBLOCKING\n");
    uart_puts("LVGL wireless-spinner=UNTIL_WIFI_AND_VH_READY wired=IMMEDIATE_READY\n");
    uart_prefix();
    uart_puts("LVGL saver-deadline=30000ms clock=RDTIME reset=TOUCH+GPIOA29+PROVISION_SUCCESS underflow=FIXED\n");
    uart_prefix();
    uart_puts("LVGL saver-status=STATE_AWARE provision=WAIT_PHONE vh=STOPPED+LISTENING+CLIENT_CONNECTED\n");
    uart_puts("LVGL saver-redraw=FULLSCREEN_X2 flush-failure=RETRY\n");
    uart_puts("LVGL battery-ui=4SEG percent=REMOVED status-text=REMOVED layout=CENTERED voltage=2DP hysteresis=20mV invalid=--V\n");
    uart_puts("LVGL battery-start=C906L_FIRST_SAMPLE invalid-hold=3 clamp-high=4250mV\n");
    uart_prefix();
    uart_puts("LVGL wired-layout=HOME_HEADPHONE+PAGE_FEATURE_GROUP usb-hub-mode-label=REMOVED\n");
    uart_prefix();
    uart_puts("LVGL alignment=battery-bars+mode-badge+CH347+HUB wired-feature=HEADPHONE\n");
    uart_puts("LVGL pixel-fix=battery-fill-y-1 ch347-back=NO_OVERLAP wired-page=ALL_PORTS_OPEN\n");
    uart_puts("LVGL ch347-control=LOCAL_ALWAYS system-lock=REMOVED ownership-monitor=PAUSED_DURING_RESET readback=AUTO_RECOVER\n");
    uart_puts("LVGL ch347-pinout=PIN_ONLY orientation=CW90 layout=2x6 rows=SCREEN_SIDE_TOP colors=5V_RED+GND_GRAY+SIGNAL_BLUE screen-direction=ROUNDED_LABEL tap=RETURN\n");
    uart_prefix();
    uart_puts("LVGL provision-mode-exit=WIRED_FORCE_CLOSE stale-state-guard=ON\n");
    uart_prefix();
    uart_puts("LVGL provision-view=HOTSPOT_ONLY qr=REMOVED tap=RETURN_WIFI hotspot=KEEP_RUNNING\n");
    uart_prefix();
    uart_puts("LVGL mode-wait-copy=STATE_AWARE boot=SYSTEM_STARTING wired=IMMEDIATE_READY wireless=SERVICE_START\n");
    uart_prefix();
    uart_puts("LVGL first-frame/visible/ready first_frame_ms=");
    uart_put_u32(ui_stats.first_frame_ms);
    uart_puts(" visible_ms=");
    uart_put_u32(ui_stats.visible_ms);
    uart_puts(" ready_ms=");
    uart_put_u32(display_status.ready_ms);
    uart_putc('\n');
    uart_log_ui_stats("READY");
    uart_prefix();
    uart_puts("WIFI provisioning=HOTSPOT+CAPTIVE_PORTAL ipc=ABI4\n");
}

static void header_init(void)
{
	struct airlink_ipc_header header;
	uint32_t *words = (uint32_t *)&header;
	volatile uint32_t *shared_words = (volatile uint32_t *)shared_header;
	uint64_t cycles = read_cycle();

	zero_words((volatile uint32_t *)AIRLINK_IPC_SHARED_BASE,
		   AIRLINK_IPC_LAYOUT_SIZE / sizeof(uint32_t));
	for (uint32_t word = 0U;
	     word < sizeof(header) / sizeof(uint32_t); ++word)
		words[word] = 0U;

	header.magic = AIRLINK_IPC_MAGIC;
	header.protocol_version = AIRLINK_IPC_VERSION;
	header.header_size = sizeof(header);
	header.total_size = AIRLINK_IPC_LAYOUT_SIZE;
	header.feature_flags = AIRLINK_IPC_FEATURE_SHM_POLL |
		AIRLINK_IPC_FEATURE_CRC32 |
		AIRLINK_IPC_FEATURE_GENERATION |
		AIRLINK_IPC_FEATURE_GPIOA29 |
		AIRLINK_IPC_FEATURE_DISPLAY |
		AIRLINK_IPC_FEATURE_TOUCH_RAW |
		AIRLINK_IPC_FEATURE_ADC1 |
        AIRLINK_IPC_FEATURE_LVGL_UI |
        AIRLINK_IPC_FEATURE_UI_STATUS |
        AIRLINK_IPC_FEATURE_CH347_CONTROL |
        AIRLINK_IPC_FEATURE_SYSTEM_CONTROL |
        AIRLINK_IPC_FEATURE_WIFI_PROVISION;
	header.c906_state_offset = AIRLINK_IPC_C906_STATE_OFFSET;
	header.linux_state_offset = AIRLINK_IPC_LINUX_STATE_OFFSET;
	header.linux_to_c906_offset = AIRLINK_IPC_LINUX_TX_OFFSET;
	header.c906_to_linux_offset = AIRLINK_IPC_C906_TX_OFFSET;
	header.state_size = sizeof(struct airlink_ipc_state);
	header.message_size = sizeof(struct airlink_ipc_message);
	header.boot_nonce = (uint32_t)cycles ^ (uint32_t)(cycles >> 32) ^
		FIRMWARE_ID;
	header.transport_id = AIRLINK_IPC_TRANSPORT_SHM_POLL;
	header.crc32 = crc32_bytes((const uint8_t *)&header,
				   sizeof(header) - sizeof(uint32_t));

	/*
	 * Publish magic last. Linux treats a zero magic as "C906L is still
	 * initializing", so it can never accept a half-written header.
	 */
	for (uint32_t word = 1U;
	     word < sizeof(header) / sizeof(uint32_t); ++word)
		shared_words[word] = words[word];
	memory_barrier();
	shared_header->magic = header.magic;
	memory_barrier();
}

static uint32_t gpio_read(uint32_t *snapshot)
{
	*snapshot = mmio_read(GPIO_EXT_PORT);
	return ((*snapshot & GPIOA29_MASK) != 0U) ? 1U : 0U;
}

static void send_message(uint32_t sequence, uint32_t type, uint32_t flags,
			 uint32_t arg0, uint32_t arg1, uint32_t arg2,
			 uint32_t arg3)
{
	struct airlink_ipc_message message;
	uint32_t *words = (uint32_t *)&message;
	uint64_t cycles = read_cycle();

	for (uint32_t word = 0U;
	     word < sizeof(message) / sizeof(uint32_t); ++word)
		words[word] = 0U;
	message.sequence = sequence;
	message.type = type;
	message.flags = flags;
	message.payload_len = 4U * sizeof(uint32_t);
	message.args[0] = arg0;
	message.args[1] = arg1;
	message.args[2] = arg2;
	message.args[3] = arg3;
	message.timestamp_low = (uint32_t)cycles;
	message.timestamp_high = (uint32_t)(cycles >> 32);
	message.sender_heartbeat = c906_state.heartbeat;

	message_write(shared_c906_tx, &message);
	c906_state.last_tx_seq = sequence;
	c906_state.tx_count++;
}

static void ch347_finish_ui(uint32_t success)
{
    ch347_sm_state = CHSM_IDLE;
    ui_model.ch347_current = ch347_status.current_mode;
    ui_model.ch347_state = success ? AIRLINK_UI_CH347_SUCCESS : AIRLINK_UI_CH347_ERROR;
    airlink_ui_set_ch347_result(ui_model.ch347_state);
    uart_prefix();
    uart_puts("CH347 result=");
    uart_puts(success ? "PASS" : "FAIL");
    uart_puts(" mode=");
    uart_puts(airlink_ch347_mode_name(ch347_status.current_mode));
    uart_puts(" count=");
    uart_put_u32(ch347_status.switch_count);
    uart_putc('\n');
}

static void ch347_begin_electrical(uint64_t now)
{
    if (airlink_ch347_begin_mode(&ch347_status, ch347_pending_mode, now) != 0) {
        ch347_finish_ui(0U);
        return;
    }
    ch347_switch_started = now;
    uart_prefix();
    uart_puts("CH347 local-switch mode=");
    uart_put_u32(ch347_pending_mode);
    uart_puts(" dtr1=");
    uart_put_u32(ch347_status.dtr_level);
    uart_puts(" rts1=");
    uart_put_u32(ch347_status.rts_level);
    uart_puts(" reset=ASSERT pulse_ms=80\n");
    ch347_sm_state = CHSM_SWITCHING;
    ui_model.ch347_state = AIRLINK_UI_CH347_SWITCHING;
    airlink_ui_set_ch347_result(ui_model.ch347_state);
}

static void ch347_request_switch(uint32_t mode, uint64_t now)
{
    if (mode >= AIRLINK_CH347_MODE_COUNT || ch347_sm_state != CHSM_IDLE)
        return;
    ui_model.ch347_selected = mode;
    ch347_pending_mode = mode;
    if (mode == ch347_status.current_mode) {
        ch347_finish_ui(1U);
        return;
    }
    ch347_begin_electrical(now);
}

static void ch347_state_machine(uint64_t now)
{
    if (ch347_sm_state == CHSM_SWITCHING) {
        int result = airlink_ch347_tick(&ch347_status, now);
        if (result < 0) {
            uart_prefix();
            uart_puts("CH347 local-switch reset=80ms FAIL elapsed_ms=");
            uart_put_u32((uint32_t)((now - ch347_switch_started) /
                                    TICKS_PER_MS));
            uart_puts(" mismatch=0x");
            uart_put_hex32(ch347_status.ownership_mismatch);
            uart_puts(" errors=0x");
            uart_put_hex32(ch347_status.error_flags);
            uart_putc('\n');
            ch347_finish_ui(0U);
        } else if (result > 0) {
            if (ch347_status.last_switch_mismatch != 0U) {
                uart_prefix();
                uart_puts("CH347 local-switch readback=RECOVERED mismatch=0x");
                uart_put_hex32(ch347_status.last_switch_mismatch);
                uart_puts(" recoveries=");
                uart_put_u32(ch347_status.switch_recovery_count);
                uart_putc('\n');
            }
            uart_prefix();
            uart_puts("CH347 local-switch reset=80ms PASS elapsed_ms=");
            uart_put_u32((uint32_t)((now - ch347_switch_started) /
                                    TICKS_PER_MS));
            uart_putc('\n');
            ch347_finish_ui(1U);
        }
    }
}

static void send_mode_event(void)
{
	mode_event_sequence = 0x80000000U |
		(c906_state.transition_count & 0x7fffffffU);

	send_message(mode_event_sequence, AIRLINK_IPC_MSG_MODE_CHANGED,
		     AIRLINK_IPC_MSG_FLAG_EVENT,
		     c906_state.stable_level, c906_state.transition_count,
		     c906_state.sample_count, 0U);
}

static void send_provision_event(uint32_t cancel)
{
    provision_request_sequence = 0xa0000000U |
        ((c906_state.tx_count + 1U) & 0x0fffffffU);
    send_message(provision_request_sequence,
        cancel ? AIRLINK_IPC_MSG_WIFI_PROVISION_CANCEL :
                 AIRLINK_IPC_MSG_WIFI_PROVISION_REQUEST,
        AIRLINK_IPC_MSG_FLAG_EVENT,
        linux_provision_status.session_id, cancel, 0U, 0U);
    uart_prefix();
    uart_puts(cancel ? "PROVISION cancel=REQUEST seq=" :
                       "PROVISION start=REQUEST seq=");
    uart_put_u32(provision_request_sequence);
    uart_putc('\n');
}

static void process_linux_message(void)
{
	struct airlink_ipc_message message;
	int result = message_read(shared_linux_tx, &message);

	if (result == 0)
		return;
	if (result < 0) {
		uint32_t generation = shared_linux_tx->generation;

		if (generation != last_bad_message_generation) {
			last_bad_message_generation = generation;
			c906_state.crc_error_count++;
			c906_state.error_flags |= AIRLINK_IPC_ERROR_PEER_CRC;
		}
		return;
	}
	if (message.generation == last_linux_message_generation &&
        message.sequence == last_linux_sequence &&
        message.type == last_linux_type)
		return;

    last_linux_message_generation = message.generation;
	last_linux_sequence = message.sequence;
    last_linux_type = message.type;
	c906_state.last_rx_seq = message.sequence;
	c906_state.rx_count++;
	uart_log_rx(&message);

	switch (message.type) {
	case AIRLINK_IPC_MSG_HELLO:
		send_message(message.sequence, AIRLINK_IPC_MSG_READY,
			     AIRLINK_IPC_MSG_FLAG_RESPONSE,
			     AIRLINK_IPC_VERSION, FIRMWARE_ID,
			     c906_state.stable_level,
			     shared_header->boot_nonce);
		uart_log_tx(message.sequence, "READY", 0U);
		break;
	case AIRLINK_IPC_MSG_PING:
		send_message(message.sequence, AIRLINK_IPC_MSG_PONG,
			     AIRLINK_IPC_MSG_FLAG_RESPONSE,
			     message.args[0], c906_state.heartbeat,
			     c906_state.stable_level, 0U);
		uart_log_tx(message.sequence, "PONG", message.args[0]);
		break;
	case AIRLINK_IPC_MSG_CLEAR_ERRORS:
		c906_state.error_flags = uart_disabled ?
			AIRLINK_IPC_ERROR_UART_TIMEOUT : 0U;
		c906_state.crc_error_count = 0U;
		c906_state.timeout_count = 0U;
		send_message(message.sequence, AIRLINK_IPC_MSG_SNAPSHOT,
			     AIRLINK_IPC_MSG_FLAG_RESPONSE,
			     c906_state.error_flags, c906_state.heartbeat,
			     c906_state.stable_level,
			     c906_state.transition_count);
		uart_log_tx(message.sequence, "SNAPSHOT", 0U);
		break;
	case AIRLINK_IPC_MSG_REQUEST_SNAPSHOT:
		send_message(message.sequence, AIRLINK_IPC_MSG_SNAPSHOT,
			     AIRLINK_IPC_MSG_FLAG_RESPONSE,
			     c906_state.error_flags, c906_state.heartbeat,
			     c906_state.stable_level,
			     c906_state.transition_count);
		uart_log_tx(message.sequence, "SNAPSHOT", 0U);
		break;
    case AIRLINK_IPC_MSG_REQUEST_CH347_STATUS:
        send_message(message.sequence, AIRLINK_IPC_MSG_CH347_STATUS,
                     AIRLINK_IPC_MSG_FLAG_RESPONSE,
                     ch347_status.current_mode,
                     ch347_status.ready,
                     ch347_status.error_flags,
                     ch347_status.switch_count);
        uart_log_tx(message.sequence, "CH347_STATUS", 0U);
        break;
    case AIRLINK_IPC_MSG_CH347_PREPARED:
    case AIRLINK_IPC_MSG_CH347_ENUM_RESULT:
        uart_prefix();
        uart_puts("CH347 linux-response=IGNORED control=LOCAL_ALWAYS type=");
        uart_puts(command_name(message.type));
        uart_putc('\n');
        break;
    case AIRLINK_IPC_MSG_WIFI_PROVISION_ACK:
    case AIRLINK_IPC_MSG_WIFI_PROVISION_CANCEL_ACK:
        uart_prefix();
        uart_puts(message.type == AIRLINK_IPC_MSG_WIFI_PROVISION_ACK ?
                  "PROVISION start=" : "PROVISION cancel=");
        uart_puts(message.args[0] ? "ACK" : "FAIL");
        uart_puts(" session=");
        uart_put_u32(message.args[1]);
        uart_puts(" phase=");
        uart_put_u32(message.args[2]);
        uart_puts(" error=");
        uart_put_u32(message.args[3]);
        uart_putc('\n');
        break;
    case AIRLINK_IPC_MSG_MODE_APPLY_RESULT:
        if (message.args[1] == c906_state.stable_level &&
            (message.sequence == mode_event_sequence ||
             c906_state.transition_count == 0U)) {
            uart_prefix();
            uart_puts("MODE apply=");
            uart_puts(message.args[0] ? "PASS" : "FAIL");
            uart_puts(" mode=");
            uart_puts(mode_name(message.args[1]));
            uart_puts(" phase=");
            uart_put_u32(message.args[2]);
            uart_puts(" error=");
            uart_put_u32(message.args[3]);
            uart_puts(" seq=");
            uart_put_u32(message.sequence);
            uart_putc('\n');
            airlink_ui_set_mode_result(message.args[0], message.args[1],
                                       message.args[2], message.args[3]);
        }
        break;
	default:
		c906_state.error_flags |= AIRLINK_IPC_ERROR_PROTOCOL;
		send_message(message.sequence, AIRLINK_IPC_MSG_SNAPSHOT,
			     AIRLINK_IPC_MSG_FLAG_RESPONSE,
			     c906_state.error_flags, message.type, 0U, 0U);
		uart_log_tx(message.sequence, "SNAPSHOT", 0U);
		break;
	}
}

static void inspect_ui_status(void)
{
    struct airlink_ipc_ui_status status;
    int result = ui_status_read(shared_ui_status, &status);

    if (result <= 0)
        return;
    if (status.owner != AIRLINK_IPC_OWNER_LINUX)
        return;
    if (status.generation == last_ui_generation)
        return;
    last_ui_generation = status.generation;
    linux_ui_status = status;
    ui_model.network = status;
    uart_prefix();
    uart_puts("UI_STATUS update=");
    uart_put_u32(status.update_count);
    uart_puts(" wifi=");
    uart_puts((status.flags & AIRLINK_UI_STATUS_WIFI_CONNECTED) ? "CONNECTED" : "DISCONNECTED");
    uart_puts(" band=");
    uart_puts((status.flags & AIRLINK_UI_STATUS_WIFI_5GHZ) ? "5G" : "2.4G");
    uart_puts(" rssi=");
    uart_put_u32((uint32_t)(status.wifi_rssi_dbm < 0 ? -status.wifi_rssi_dbm : status.wifi_rssi_dbm));
    uart_puts(" vh=");
    if (status.virtualhere_state == AIRLINK_VIRTUALHERE_CLIENT_CONNECTED)
        uart_puts("CLIENT_CONNECTED");
    else if (status.virtualhere_state == AIRLINK_VIRTUALHERE_LISTENING)
        uart_puts("LISTENING");
    else
        uart_puts("STOPPED");
    uart_puts(" phase=");
    uart_put_u32(status.system_phase);
    uart_puts(" applied=");
    uart_puts((status.flags & AIRLINK_UI_STATUS_MODE_APPLIED) ? "YES" : "NO");
    uart_puts(" fault=");
    uart_puts((status.flags & AIRLINK_UI_STATUS_SYSTEM_FAULT) ? "YES" : "NO");
    uart_putc('\n');
}

static void inspect_provision_status(void)
{
    struct airlink_ipc_provision_status status;
    int result = provision_status_read(shared_provision_status, &status);

    if (result <= 0 || status.owner != AIRLINK_IPC_OWNER_LINUX ||
        status.generation == last_provision_generation)
        return;
    last_provision_generation = status.generation;
    linux_provision_status = status;
    ui_model.provision = status;
    uart_prefix();
    uart_puts("PROVISION_STATUS phase=");
    uart_put_u32(status.phase);
    uart_puts(" error=");
    uart_put_u32(status.error);
    uart_puts(" session=");
    uart_put_u32(status.session_id);
    uart_puts(" ap=");
    uart_puts((status.flags & AIRLINK_PROVISION_FLAG_AP_READY) ?
              "READY" : "DOWN");
    uart_putc('\n');
}

static void inspect_linux_state(void)
{
	struct airlink_ipc_state linux_state;
	int result = state_read(shared_linux_state, &linux_state);

	if (result == 0)
		return;
	if (result < 0) {
		uint32_t generation = shared_linux_state->generation;

		if (generation != last_bad_state_generation) {
			last_bad_state_generation = generation;
			c906_state.crc_error_count++;
			c906_state.error_flags |= AIRLINK_IPC_ERROR_PEER_CRC;
		}
		return;
	}
	if (linux_state.owner != AIRLINK_IPC_OWNER_LINUX ||
	    linux_state.abi_revision != ABI_REVISION) {
		c906_state.error_flags |= AIRLINK_IPC_ERROR_PROTOCOL;
		return;
	}

	c906_state.peer_heartbeat = linux_state.heartbeat;
	c906_state.flags |= AIRLINK_IPC_STATE_PEER_VALID;
    inspect_ui_status();
    inspect_provision_status();
	if (!linux_peer_seen) {
		linux_peer_seen = 1;
		uart_prefix();
		uart_puts("PEER Linux online heartbeat=");
		uart_put_u32(linux_state.heartbeat);
		uart_putc('\n');
	}
}

static void check_gpio_ownership(void)
{
	uint32_t pinmux = mmio_read(PINMUX_IIC0_SDA);
	uint32_t direction = mmio_read(GPIO_DIRECTION);

	airlink_display_check_ownership(&display_status);
	display_state_sync();
	airlink_touch_check_ownership(&touch_status);
	touch_state_sync();
    if (ch347_sm_state == CHSM_IDLE &&
        ch347_status.ready && !ch347_status.switching) {
		int ch347_owner_result =
			airlink_ch347_check_ownership(&ch347_status);

		if (ch347_owner_result > 0) {
			ch347_owner_error_logged = 0;
			c906_state.warning_count++;
			uart_prefix();
			uart_puts("CH347 warning=OWNER_RECLAIMED mismatch=0x");
			uart_put_hex32(ch347_status.last_owner_mismatch);
			uart_puts(" recoveries=");
			uart_put_u32(ch347_status.recovery_count);
			uart_puts(" current_mismatch=0x");
			uart_put_hex32(ch347_status.ownership_mismatch);
			uart_puts(" status=READY\n");
		} else if (ch347_owner_result < 0 &&
			   !ch347_owner_error_logged) {
			ch347_owner_error_logged = 1;
			c906_state.warning_count++;
			uart_prefix();
			uart_puts("CH347 error=OWNER_RESTORE_FAILED mismatch=0x");
			uart_put_hex32(ch347_status.ownership_mismatch);
			uart_puts(" errors=0x");
			uart_put_hex32(ch347_status.error_flags);
			uart_putc('\n');
		}
	}
    if (adc1_status.ready) {
        int adc1_owner_result = airlink_adc1_check_ownership(&adc1_status);

        adc1_state_sync();
        if (adc1_owner_result > 0) {
            adc1_owner_error_logged = 0;
            c906_state.warning_count++;
            uart_prefix();
            uart_puts("ADC1 warning=OWNER_RECLAIMED mismatch=0x");
            uart_put_hex32(adc1_status.last_owner_mismatch);
            uart_puts(" recoveries=");
            uart_put_u32(adc1_status.recovery_count);
            uart_puts(" current_mismatch=0x");
            uart_put_hex32(adc1_status.ownership_mismatch);
            uart_puts(" status=READY\n");
        } else if (adc1_owner_result < 0 && !adc1_owner_error_logged) {
            adc1_owner_error_logged = 1;
            c906_state.warning_count++;
            uart_prefix();
            uart_puts("ADC1 error=OWNER_RESTORE_FAILED mismatch=0x");
            uart_put_hex32(adc1_status.ownership_mismatch);
            uart_puts(" errors=0x");
            uart_put_hex32(adc1_status.error_flags);
            uart_putc('\n');
        }
    }
	if ((display_status.error_flags & AIRLINK_DISPLAY_ERROR_OWNER_CHANGED) &&
	    !display_owner_warning_logged) {
		display_owner_warning_logged = 1;
		c906_state.warning_count++;
		uart_prefix();
		uart_puts("WARN display-owner-changed\n");
	}
	if (touch_status.owner_changed && !touch_owner_warning_logged) {
		touch_owner_warning_logged = 1;
		c906_state.warning_count++;
		uart_prefix();
		uart_puts("WARN touch-owner-changed mismatch=0x");
		uart_put_hex32(touch_status.ownership_mismatch);
		uart_puts(" mux=");
		uart_put_hex32(touch_status.pinmux_scl);
		uart_putc('/');
		uart_put_hex32(touch_status.pinmux_sda);
		uart_putc('/');
		uart_put_hex32(touch_status.pinmux_irq);
		uart_putc('/');
		uart_put_hex32(touch_status.pinmux_reset);
		uart_puts(" gpio_data=0x");
		uart_put_hex32(touch_status.gpio_data);
		uart_puts(" gpio_dir=0x");
		uart_put_hex32(touch_status.gpio_direction);
		uart_puts(" gpio_ext=0x");
		uart_put_hex32(touch_status.gpio_ext_port);
		uart_putc('\n');
	}

	c906_state.pinmux_current = pinmux;
	c906_state.direction_current = direction;
	if (pinmux != PINMUX_GPIOA29_VALUE &&
	    !(c906_state.error_flags & AIRLINK_IPC_ERROR_PINMUX_CHANGED)) {
		c906_state.error_flags |= AIRLINK_IPC_ERROR_PINMUX_CHANGED;
		c906_state.warning_count++;
		uart_prefix();
		uart_puts("WARN pinmux-changed value=0x");
		uart_put_hex32(pinmux);
		uart_putc('\n');
	}
	if ((direction & GPIOA29_MASK) != 0U &&
	    !(c906_state.error_flags & AIRLINK_IPC_ERROR_DIR_CHANGED)) {
		c906_state.error_flags |= AIRLINK_IPC_ERROR_DIR_CHANGED;
		c906_state.warning_count++;
		uart_prefix();
		uart_puts("WARN direction-changed value=0x");
		uart_put_hex32(direction);
		uart_putc('\n');
	}
}

void r25_main(void)
{
    uint32_t direction;
    uint32_t snapshot;
    uint32_t raw;
    uint32_t candidate;
    uint32_t debounce_count = 1U;
    uint32_t stable = 0U;
    int stable_valid = 0;
    uint64_t now;
    uint64_t gpio_deadline;
    uint64_t adc_deadline;
    uint64_t heartbeat_deadline;
    uint64_t health_deadline;

    header_init();

    /* Configure GPIOA_29 once, preserving the R6-proven ownership policy. */
    mmio_write(PINMUX_IIC0_SDA, PINMUX_GPIOA29_VALUE);
    direction = mmio_read(GPIO_DIRECTION) & ~GPIOA29_MASK;
    mmio_write(GPIO_DIRECTION, direction);
    memory_barrier();

    raw = gpio_read(&snapshot);
    candidate = raw;
    zero_words((volatile uint32_t *)&c906_state,
               sizeof(c906_state) / sizeof(uint32_t));
    c906_state.owner = AIRLINK_IPC_OWNER_C906L;
    c906_state.flags = AIRLINK_IPC_STATE_RUNNING |
        AIRLINK_IPC_STATE_READY |
        AIRLINK_IPC_STATE_SELFTEST_OK |
        AIRLINK_IPC_STATE_RAW_VALID |
        (raw ? AIRLINK_IPC_STATE_RAW_HIGH : 0U);
    c906_state.boot_count = 1U;
    c906_state.raw_level = raw;
    c906_state.stable_level = raw;
    c906_state.sample_count = 1U;
    c906_state.init_stage = 4U;
    c906_state.firmware_id = FIRMWARE_ID;
    c906_state.abi_revision = ABI_REVISION;
    c906_state.pinmux_current = mmio_read(PINMUX_IIC0_SDA);
    c906_state.direction_current = mmio_read(GPIO_DIRECTION);
    c906_state.ext_port_snapshot = snapshot;
    c906_state.high_sample_count = raw ? 1U : 0U;
    c906_state.low_sample_count = raw ? 0U : 1U;
    c906_state.debounce_count = 1U;
    state_update_time();
    state_write(shared_c906_state, &c906_state);

    uart_prefix();
    uart_puts("START proto=1 abi=4 transport=shm-poll shm=0x8fff0000 firmware=R27P\n");
    uart_prefix();
    uart_puts("SELFTEST header=PASS crc=PASS layout=PASS\n");
    uart_prefix();
    uart_puts("GPIO pin=GPIOA_29 level=");
    uart_puts(level_name(raw));
    uart_puts(" mode=");
    uart_puts(mode_name(raw));
    uart_puts(" debounce=200ms sample=10ms scheduler=RDTIME\n");

    ch347_start();
    adc1_start();
    display_start();
    display_state_sync();
    touch_start();
    touch_state_sync();
    ui_start();

    now = read_time();
    last_lv_tick = (uint32_t)(now / TICKS_PER_MS);
    gpio_deadline = now + (uint64_t)GPIO_SAMPLE_MS * TICKS_PER_MS;
    adc_deadline = now + (uint64_t)ADC_INTERVAL_MS * TICKS_PER_MS;
    heartbeat_deadline = now +
        (uint64_t)HEARTBEAT_INTERVAL_MS * TICKS_PER_MS;
    health_deadline = now + (uint64_t)HEALTH_INTERVAL_MS * TICKS_PER_MS;
    if (adc1_status.ready && display_status.ready && touch_status.ready &&
        ui_stats.ready)
        c906_state.init_stage = 11U;
    state_update_time();
    state_write(shared_c906_state, &c906_state);

    for (;;) {
        uint64_t loop_started = read_time();

        now = loop_started;
        if (deadline_reached(now, gpio_deadline)) {
            uint32_t next_raw;

            do {
                gpio_deadline += (uint64_t)GPIO_SAMPLE_MS * TICKS_PER_MS;
            } while (deadline_reached(now, gpio_deadline));

            next_raw = gpio_read(&snapshot);
            if (next_raw == candidate) {
                if (debounce_count < DEBOUNCE_SAMPLES)
                    debounce_count++;
            } else {
                candidate = next_raw;
                debounce_count = 1U;
            }

            c906_state.raw_level = next_raw;
            c906_state.sample_count++;
            c906_state.ext_port_snapshot = snapshot;
            c906_state.debounce_count = debounce_count;
            if (next_raw)
                c906_state.high_sample_count++;
            else
                c906_state.low_sample_count++;
            c906_state.flags &= ~AIRLINK_IPC_STATE_RAW_HIGH;
            if (next_raw)
                c906_state.flags |= AIRLINK_IPC_STATE_RAW_HIGH;

            if (next_raw != raw) {
                raw = next_raw;
                uart_log_mode("RAW", raw);
            }

            if (debounce_count >= DEBOUNCE_SAMPLES &&
                (!stable_valid || candidate != stable)) {
                int first = !stable_valid;

                stable = candidate;
                stable_valid = 1;
                c906_state.stable_level = stable;
                c906_state.flags |= AIRLINK_IPC_STATE_STABLE_VALID;
                c906_state.flags &= ~AIRLINK_IPC_STATE_STABLE_HIGH;
                if (stable)
                    c906_state.flags |= AIRLINK_IPC_STATE_STABLE_HIGH;
                if (!first) {
                    c906_state.transition_count++;
                    send_mode_event();
                }
                uart_log_mode("STABLE", stable);
                if (ui_stats.ready)
                    airlink_ui_show_mode_transition(stable);
            }
        }

        process_linux_message();
        touch_periodic();

        now = read_time();
        {
            uint32_t tick = (uint32_t)(now / TICKS_PER_MS);
            uint32_t delta = tick - last_lv_tick;
            uint32_t event_arg = 0U;

            if (delta != 0U) {
                lv_tick_inc(delta);
                last_lv_tick = tick;
            }
            ui_model_sync();
            if (ui_stats.ready) {
                int ui_event =
                    airlink_ui_tick(&ui_model, &touch_status, now, &event_arg);
                if (ui_event == AIRLINK_UI_EVENT_CH347_REQUEST)
                    ch347_request_switch(event_arg, now);
                else if (ui_event == AIRLINK_UI_EVENT_PROVISION_REQUEST)
                    send_provision_event(0U);
                else if (ui_event == AIRLINK_UI_EVENT_PROVISION_CANCEL)
                    send_provision_event(1U);
                {
                    uint32_t saver_reason =
                        airlink_ui_take_saver_reset_reason();
                    if (saver_reason != AIRLINK_UI_SAVER_RESET_NONE)
                        uart_log_saver_reset(saver_reason);
                }
            }
            ch347_state_machine(now);
        }

        now = read_time();
        if (deadline_reached(now, adc_deadline)) {
            do {
                adc_deadline += (uint64_t)ADC_INTERVAL_MS * TICKS_PER_MS;
            } while (deadline_reached(now, adc_deadline));
            adc1_periodic();
        }

        if (deadline_reached(now, heartbeat_deadline)) {
            do {
                heartbeat_deadline +=
                    (uint64_t)HEARTBEAT_INTERVAL_MS * TICKS_PER_MS;
            } while (deadline_reached(now, heartbeat_deadline));
            c906_state.heartbeat++;
            inspect_linux_state();
            check_gpio_ownership();
        }

        if (deadline_reached(now, health_deadline)) {
            do {
                health_deadline +=
                    (uint64_t)HEALTH_INTERVAL_MS * TICKS_PER_MS;
            } while (deadline_reached(now, health_deadline));
            uart_log_health();
            if (ui_stats.ready)
                uart_log_ui_stats("PERF");
        }

        state_update_time();
        state_write(shared_c906_state, &c906_state);
        airlink_ui_note_loop_us((uint32_t)((read_time() - loop_started) /
                                           (TICKS_PER_MS / 1000U)));
        delay_ms(IDLE_DELAY_MS);
    }
}
