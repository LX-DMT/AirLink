#ifndef AIRLINK_ADC1_H
#define AIRLINK_ADC1_H

#include <stdint.h>

#define AIRLINK_ADC1_ERROR_TIMEOUT          (1U << 0)
#define AIRLINK_ADC1_ERROR_PINMUX_CHANGED   (1U << 1)
#define AIRLINK_ADC1_ERROR_CLOCK_CHANGED    (1U << 2)
#define AIRLINK_ADC1_ERROR_RESET_CHANGED    (1U << 3)
#define AIRLINK_ADC1_ERROR_RESULT_RANGE     (1U << 4)

#define AIRLINK_ADC1_OWNER_PINMUX           (1U << 0)
#define AIRLINK_ADC1_OWNER_CLOCK            (1U << 1)
#define AIRLINK_ADC1_OWNER_RESET            (1U << 2)
#define AIRLINK_ADC1_OWNER_CHANNEL          (1U << 3)

#define AIRLINK_ADC1_DISPLAY_NONE           0U
#define AIRLINK_ADC1_DISPLAY_INITIAL        1U
#define AIRLINK_ADC1_DISPLAY_NORMAL         2U
#define AIRLINK_ADC1_DISPLAY_HELD           3U
#define AIRLINK_ADC1_DISPLAY_CLAMP_HIGH     4U
#define AIRLINK_ADC1_DISPLAY_INVALID        5U

struct airlink_adc1_status {
	uint32_t ready;
	uint32_t error_flags;
	uint32_t ownership_mismatch;
	uint32_t last_owner_mismatch;
	uint32_t sample_count;
	uint32_t batch_count;
	uint32_t timeout_count;
	uint32_t recovery_count;
	uint32_t raw_latest;
	uint32_t raw_min;
	uint32_t raw_max;
	uint32_t raw_average;
	uint32_t raw_filtered;
	uint32_t raw_history[3];
	uint32_t raw_history_index;
	uint32_t raw_history_count;
	uint32_t measurement_valid;
	uint32_t invalid_count;
	uint32_t invalid_streak;
	uint32_t display_valid;
	uint32_t display_source;
	uint32_t adc_millivolts;
	uint32_t battery_millivolts;
	uint32_t display_battery_millivolts;
	uint32_t nominal_percent;
	uint32_t pinmux;
	uint32_t clock_enable;
	uint32_t reset_state;
	uint32_t control;
	uint32_t status;
	uint32_t cycle_setting;
	uint32_t result_register;
};

int airlink_adc1_init(struct airlink_adc1_status *status);
int airlink_adc1_sample_batch(struct airlink_adc1_status *status);
void airlink_adc1_note_failure(struct airlink_adc1_status *status);
int airlink_adc1_check_ownership(struct airlink_adc1_status *status);

#endif
