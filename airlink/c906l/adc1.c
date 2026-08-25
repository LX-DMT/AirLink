#include <stdint.h>

#include "adc1.h"

/* SG2002/CV181x top-domain SARADC and ADC1 pad. */
#define PINMUX_ADC1             0x030010f8UL
#define PINMUX_ADC_FUNCTION     0U

#define CLOCK_ENABLE_0          0x03002000UL
#define CLOCK_SARADC_MASK       (1U << 10)

#define RESET_BASE              0x03003000UL
#define RESET_SARADC_REGISTER   (RESET_BASE + 0x04)
#define RESET_SARADC_MASK       (1U << 20) /* reset ID 52 */

#define SARADC_BASE             0x030f0000UL
#define SARADC_CTRL             (SARADC_BASE + 0x004)
#define SARADC_STATUS           (SARADC_BASE + 0x008)
#define SARADC_CYC_SET          (SARADC_BASE + 0x00c)
#define SARADC_CH1_RESULT       (SARADC_BASE + 0x014)
#define SARADC_INTR_EN          (SARADC_BASE + 0x020)
#define SARADC_INTR_CLR         (SARADC_BASE + 0x024)

#define SARADC_START            (1U << 0)
#define SARADC_CHANNEL1         (1U << 5)
#define SARADC_BUSY             (1U << 0)
#define SARADC_RESULT_MASK      0x0fffU
#define SARADC_CYCLE_MASK       (0x0fU << 12)
#define SARADC_CYCLE_840NS      (0x0fU << 12)
#define SARADC_TIMEOUT_LOOPS    100000U
#define ADC_BATCH_SAMPLES       16U

/*
 * Keep the R17 single reference point, but calculate VBAT directly from the
 * filtered raw code.  This avoids the old synthetic ADC-full-scale/divider
 * path and its misleading precision.  The value remains an estimate.
 */
#define ADC_CALIBRATION_RAW         847U
#define ADC_CALIBRATION_BATTERY_MV  4100U
#define BATTERY_VALID_MIN_MV        2800U
#define BATTERY_VALID_MAX_MV        4250U
#define BATTERY_INVALID_CONFIRM_BATCHES 3U
#define BATTERY_EMPTY_MV            3300U
#define BATTERY_FULL_MV             4200U

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

static void zero_status(struct airlink_adc1_status *status)
{
	uint32_t *words = (uint32_t *)status;

	for (uint32_t index = 0U;
	     index < sizeof(*status) / sizeof(uint32_t); ++index)
		words[index] = 0U;
}

static void capture_registers(struct airlink_adc1_status *status)
{
	status->pinmux = mmio_read(PINMUX_ADC1);
	status->clock_enable = mmio_read(CLOCK_ENABLE_0);
	status->reset_state = mmio_read(RESET_SARADC_REGISTER);
	status->control = mmio_read(SARADC_CTRL);
	status->status = mmio_read(SARADC_STATUS);
	status->cycle_setting = mmio_read(SARADC_CYC_SET);
	status->result_register = mmio_read(SARADC_CH1_RESULT);
}

static void claim_hardware(void)
{
	uint32_t value;

	/* Put the package pin in its native analogue ADC1 function. */
	mmio_write(PINMUX_ADC1, PINMUX_ADC_FUNCTION);

	/* clk_saradc: clock-controller register 0, bit 10. */
	value = mmio_read(CLOCK_ENABLE_0);
	mmio_write(CLOCK_ENABLE_0, value | CLOCK_SARADC_MASK);

	/* RST_SARADC is active-low, reset ID 52 => bank 1, bit 20. */
	value = mmio_read(RESET_SARADC_REGISTER);
	mmio_write(RESET_SARADC_REGISTER, value | RESET_SARADC_MASK);
	memory_barrier();

	/* Match the vendor U-Boot/Linux polling driver configuration. */
	mmio_write(SARADC_INTR_EN, 0U);
	mmio_write(SARADC_INTR_CLR, 1U);
	value = mmio_read(SARADC_CYC_SET);
	value &= ~SARADC_CYCLE_MASK;
	value |= SARADC_CYCLE_840NS;
	mmio_write(SARADC_CYC_SET, value);
	mmio_write(SARADC_CTRL, SARADC_CHANNEL1);
	memory_barrier();
}

static uint32_t ownership_mismatch(const struct airlink_adc1_status *status)
{
	uint32_t mismatch = 0U;

	if (status->pinmux != PINMUX_ADC_FUNCTION)
		mismatch |= AIRLINK_ADC1_OWNER_PINMUX;
	if ((status->clock_enable & CLOCK_SARADC_MASK) == 0U)
		mismatch |= AIRLINK_ADC1_OWNER_CLOCK;
	if ((status->reset_state & RESET_SARADC_MASK) == 0U)
		mismatch |= AIRLINK_ADC1_OWNER_RESET;
	if ((status->control & SARADC_CHANNEL1) == 0U)
		mismatch |= AIRLINK_ADC1_OWNER_CHANNEL;
	return mismatch;
}

static int sample_once(struct airlink_adc1_status *status, uint32_t *raw)
{
	uint32_t control = mmio_read(SARADC_CTRL);
	uint32_t timeout = SARADC_TIMEOUT_LOOPS;

	/* Generate an explicit low-to-high start edge while keeping CH1 selected. */
	control &= ~SARADC_START;
	control |= SARADC_CHANNEL1;
	mmio_write(SARADC_CTRL, control);
	mmio_write(SARADC_CTRL, control | SARADC_START);
	memory_barrier();

	while ((mmio_read(SARADC_STATUS) & SARADC_BUSY) != 0U) {
		if (--timeout == 0U) {
			status->timeout_count++;
			status->error_flags |= AIRLINK_ADC1_ERROR_TIMEOUT;
			capture_registers(status);
			return -1;
		}
	}

	*raw = mmio_read(SARADC_CH1_RESULT) & SARADC_RESULT_MASK;
	status->raw_latest = *raw;
	status->sample_count++;
	if (*raw > SARADC_RESULT_MASK) {
		status->error_flags |= AIRLINK_ADC1_ERROR_RESULT_RANGE;
		return -1;
	}
	return 0;
}

static uint32_t median3(uint32_t a, uint32_t b, uint32_t c)
{
	if (a > b) {
		uint32_t temporary = a;
		a = b;
		b = temporary;
	}
	if (b > c) {
		uint32_t temporary = b;
		b = c;
		c = temporary;
	}
	if (a > b)
		b = a;
	return b;
}

static uint32_t nominal_percent_from_mv(uint32_t millivolts)
{
	if (millivolts <= BATTERY_EMPTY_MV)
		return 0U;
	if (millivolts >= BATTERY_FULL_MV)
		return 100U;
	return ((millivolts - BATTERY_EMPTY_MV) * 100U +
		(BATTERY_FULL_MV - BATTERY_EMPTY_MV) / 2U) /
		(BATTERY_FULL_MV - BATTERY_EMPTY_MV);
}

void airlink_adc1_note_failure(struct airlink_adc1_status *status)
{
	if (status->invalid_streak < BATTERY_INVALID_CONFIRM_BATCHES)
		status->invalid_streak++;
	status->invalid_count++;
	if (status->display_valid &&
	    status->invalid_streak < BATTERY_INVALID_CONFIRM_BATCHES) {
		status->display_source = AIRLINK_ADC1_DISPLAY_HELD;
		return;
	}
	status->display_valid = 0U;
	status->display_source = AIRLINK_ADC1_DISPLAY_INVALID;
	status->nominal_percent = 0U;
}

int airlink_adc1_init(struct airlink_adc1_status *status)
{
	zero_status(status);
	claim_hardware();
	capture_registers(status);
	status->ownership_mismatch = ownership_mismatch(status);
	if (status->ownership_mismatch != 0U)
		return -1;
	status->ready = 1U;
	return airlink_adc1_sample_batch(status);
}

int airlink_adc1_sample_batch(struct airlink_adc1_status *status)
{
	uint32_t minimum = SARADC_RESULT_MASK;
	uint32_t maximum = 0U;
	uint32_t sum = 0U;
	uint32_t raw;
	uint32_t average;

	if (!status->ready)
		return -1;
	for (uint32_t index = 0U; index < ADC_BATCH_SAMPLES; ++index) {
		if (sample_once(status, &raw) != 0) {
			airlink_adc1_note_failure(status);
			return -1;
		}
		if (raw < minimum)
			minimum = raw;
		if (raw > maximum)
			maximum = raw;
		sum += raw;
	}

	average = (sum - minimum - maximum +
		(ADC_BATCH_SAMPLES - 2U) / 2U) / (ADC_BATCH_SAMPLES - 2U);
	status->raw_min = minimum;
	status->raw_max = maximum;
	status->raw_average = average;
	status->raw_history[status->raw_history_index] = average;
	status->raw_history_index =
		(status->raw_history_index + 1U) % 3U;
	if (status->raw_history_count < 3U)
		status->raw_history_count++;
	if (status->raw_history_count == 1U) {
		status->raw_filtered = average;
	} else if (status->raw_history_count == 2U) {
		status->raw_filtered =
			(status->raw_history[0] + status->raw_history[1] + 1U) / 2U;
	} else {
		status->raw_filtered =
			median3(status->raw_history[0],
				status->raw_history[1],
				status->raw_history[2]);
	}
	status->batch_count++;
	status->battery_millivolts =
		(status->raw_filtered * ADC_CALIBRATION_BATTERY_MV +
		 ADC_CALIBRATION_RAW / 2U) / ADC_CALIBRATION_RAW;
	status->adc_millivolts =
		(status->battery_millivolts + 2U) / 4U;
	status->measurement_valid =
		status->battery_millivolts >= BATTERY_VALID_MIN_MV &&
		status->battery_millivolts <= BATTERY_VALID_MAX_MV;
	if (status->battery_millivolts > BATTERY_VALID_MAX_MV) {
		status->invalid_count++;
		status->invalid_streak = 0U;
		status->display_battery_millivolts = BATTERY_VALID_MAX_MV;
		status->display_valid = 1U;
		status->display_source = AIRLINK_ADC1_DISPLAY_CLAMP_HIGH;
	} else if (status->measurement_valid) {
		status->invalid_streak = 0U;
		status->display_battery_millivolts =
			status->battery_millivolts;
		status->display_valid = 1U;
		status->display_source = status->batch_count == 1U ?
			AIRLINK_ADC1_DISPLAY_INITIAL :
			AIRLINK_ADC1_DISPLAY_NORMAL;
	} else {
		airlink_adc1_note_failure(status);
	}
	status->nominal_percent = status->display_valid ?
		nominal_percent_from_mv(status->display_battery_millivolts) : 0U;
	capture_registers(status);
	return 0;
}

int airlink_adc1_check_ownership(struct airlink_adc1_status *status)
{
	uint32_t mismatch;

	capture_registers(status);
	mismatch = ownership_mismatch(status);
	status->ownership_mismatch = mismatch;
	if (mismatch == 0U)
		return 0;
	status->last_owner_mismatch = mismatch;
	claim_hardware();
	capture_registers(status);
	status->ownership_mismatch = ownership_mismatch(status);
	if (status->ownership_mismatch != 0U) {
		if (status->ownership_mismatch & AIRLINK_ADC1_OWNER_PINMUX)
			status->error_flags |= AIRLINK_ADC1_ERROR_PINMUX_CHANGED;
		if (status->ownership_mismatch & AIRLINK_ADC1_OWNER_CLOCK)
			status->error_flags |= AIRLINK_ADC1_ERROR_CLOCK_CHANGED;
		if (status->ownership_mismatch & AIRLINK_ADC1_OWNER_RESET)
			status->error_flags |= AIRLINK_ADC1_ERROR_RESET_CHANGED;
		return -1;
	}
	status->recovery_count++;
	status->ready = 1U;
	return 1;
}
