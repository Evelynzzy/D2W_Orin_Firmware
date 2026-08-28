#include "gd32f30x.h"
#include "board_pins.h"
#include "systick.h"
#include "voltage_monitor.h"

#define VOLTAGE_MONITOR_GAIN_PPM_UNITY    (1000000UL)
#define VOLTAGE_MONITOR_UINT32_MAX_VALUE    (0xFFFFFFFFUL)

#if (VOLTAGE_MONITOR_SAMPLE_COUNT < 5U)
#error "VOLTAGE_MONITOR_SAMPLE_COUNT must be at least 5"
#endif

#if (VOLTAGE_MONITOR_SAMPLE_COUNT > 64U)
#error "VOLTAGE_MONITOR_SAMPLE_COUNT must not exceed 64"
#endif

#if ((2U * VOLTAGE_MONITOR_TRIM_COUNT) >= VOLTAGE_MONITOR_SAMPLE_COUNT)
#error "VOLTAGE_MONITOR_TRIM_COUNT removes all ADC samples"
#endif

#if (VOLTAGE_MONITOR_FAULT_ASSERT_COUNT == 0U)
#error "VOLTAGE_MONITOR_FAULT_ASSERT_COUNT must be greater than zero"
#endif

#if (VOLTAGE_MONITOR_FAULT_CLEAR_COUNT == 0U)
#error "VOLTAGE_MONITOR_FAULT_CLEAR_COUNT must be greater than zero"
#endif

#if (VOLTAGE_MONITOR_ADC_ERROR_ASSERT_COUNT == 0U)
#error "VOLTAGE_MONITOR_ADC_ERROR_ASSERT_COUNT must be greater than zero"
#endif

#if (VOLTAGE_MONITOR_ADC_ERROR_CLEAR_COUNT == 0U)
#error "VOLTAGE_MONITOR_ADC_ERROR_CLEAR_COUNT must be greater than zero"
#endif

#if (VOLTAGE_MONITOR_UPDATE_PERIOD_MS == 0UL)
#error "VOLTAGE_MONITOR_UPDATE_PERIOD_MS must be greater than zero"
#endif

#if ((VOLTAGE_MONITOR_ENABLE_SOFTWARE_TEST != 0U) && (VOLTAGE_MONITOR_ENABLE_SOFTWARE_TEST != 1U))
#error "VOLTAGE_MONITOR_ENABLE_SOFTWARE_TEST must be 0 or 1"
#endif

#if (VOLTAGE_MONITOR_ADC_REFERENCE_MV == 0UL) || (VOLTAGE_MONITOR_ADC_FULL_SCALE == 0UL)
#error "ADC reference and full scale must be greater than zero"
#endif

#if (VOLTAGE_MONITOR_ORIN_DIVIDER_DEN == 0UL) || (VOLTAGE_MONITOR_5V_DIVIDER_DEN == 0UL) || (VOLTAGE_MONITOR_3V3_DIVIDER_DEN == 0UL) || (VOLTAGE_MONITOR_1V8_DIVIDER_DEN == 0UL)
#error "Voltage-divider denominators must be greater than zero"
#endif

#if (VOLTAGE_MONITOR_ORIN_MIN_MV >= VOLTAGE_MONITOR_ORIN_MAX_MV) || (VOLTAGE_MONITOR_5V_MIN_MV >= VOLTAGE_MONITOR_5V_MAX_MV) || (VOLTAGE_MONITOR_3V3_MIN_MV >= VOLTAGE_MONITOR_3V3_MAX_MV) || (VOLTAGE_MONITOR_1V8_MIN_MV >= VOLTAGE_MONITOR_1V8_MAX_MV)
#error "Every rail minimum must be lower than its maximum"
#endif

typedef struct
{
	uint8_t adc_channel;
	uint32_t divider_numerator;
	uint32_t divider_denominator;
	uint32_t gain_ppm;
	int32_t offset_mv;
	uint32_t minimum_mv;
	uint32_t maximum_mv;
	uint32_t hysteresis_mv;
	uint32_t fault_bit;
} voltage_monitor_channel_config_t;

static const voltage_monitor_channel_config_t s_channel_config[VOLTAGE_MONITOR_CHANNEL_COUNT] =
{
	{
		(uint8_t)ORIN_VDD_SENSE_CHANNEL,
		VOLTAGE_MONITOR_ORIN_DIVIDER_NUM,
		VOLTAGE_MONITOR_ORIN_DIVIDER_DEN,
		VOLTAGE_MONITOR_ORIN_GAIN_PPM,
		VOLTAGE_MONITOR_ORIN_OFFSET_MV,
		VOLTAGE_MONITOR_ORIN_MIN_MV,
		VOLTAGE_MONITOR_ORIN_MAX_MV,
		VOLTAGE_MONITOR_ORIN_HYST_MV,
		VOLTAGE_FAULT_ORIN_VDD
	},
	{
		(uint8_t)SYSTEM_5V_SENSE_CHANNEL,
		VOLTAGE_MONITOR_5V_DIVIDER_NUM,
		VOLTAGE_MONITOR_5V_DIVIDER_DEN,
		VOLTAGE_MONITOR_5V_GAIN_PPM,
		VOLTAGE_MONITOR_5V_OFFSET_MV,
		VOLTAGE_MONITOR_5V_MIN_MV,
		VOLTAGE_MONITOR_5V_MAX_MV,
		VOLTAGE_MONITOR_5V_HYST_MV,
		VOLTAGE_FAULT_5V
	},
	{
		(uint8_t)SYSTEM_3V3_SENSE_CHANNEL,
		VOLTAGE_MONITOR_3V3_DIVIDER_NUM,
		VOLTAGE_MONITOR_3V3_DIVIDER_DEN,
		VOLTAGE_MONITOR_3V3_GAIN_PPM,
		VOLTAGE_MONITOR_3V3_OFFSET_MV,
		VOLTAGE_MONITOR_3V3_MIN_MV,
		VOLTAGE_MONITOR_3V3_MAX_MV,
		VOLTAGE_MONITOR_3V3_HYST_MV,
		VOLTAGE_FAULT_3V3
	},
	{
		(uint8_t)SYSTEM_1V8_SENSE_CHANNEL,
		VOLTAGE_MONITOR_1V8_DIVIDER_NUM,
		VOLTAGE_MONITOR_1V8_DIVIDER_DEN,
		VOLTAGE_MONITOR_1V8_GAIN_PPM,
		VOLTAGE_MONITOR_1V8_OFFSET_MV,
		VOLTAGE_MONITOR_1V8_MIN_MV,
		VOLTAGE_MONITOR_1V8_MAX_MV,
		VOLTAGE_MONITOR_1V8_HYST_MV,
		VOLTAGE_FAULT_1V8
	}
};

volatile uint16_t adc_orin_vdd_raw = 0U;
volatile uint16_t adc_5v_raw = 0U;
volatile uint16_t adc_3v3_raw = 0U;
volatile uint16_t adc_1v8_raw = 0U;

volatile float orin_vdd_voltage = 0.0f;
volatile float system_5v_voltage = 0.0f;
volatile float system_3v3_voltage = 0.0f;
volatile float system_1v8_voltage = 0.0f;

volatile uint32_t voltage_fault_flags = VOLTAGE_FAULT_NONE;

volatile uint32_t orin_vdd_mv = 0U;
volatile uint32_t system_5v_mv = 0U;
volatile uint32_t system_3v3_mv = 0U;
volatile uint32_t system_1v8_mv = 0U;

volatile voltage_monitor_state_t g_voltage_monitor_state = VOLTAGE_MONITOR_STATE_UNINITIALIZED;
volatile voltage_monitor_result_t g_voltage_monitor_init_result = VOLTAGE_MONITOR_RESULT_NOT_INITIALIZED;
volatile voltage_monitor_result_t g_voltage_monitor_last_result = VOLTAGE_MONITOR_RESULT_NOT_INITIALIZED;

volatile uint8_t g_voltage_monitor_initialized = 0U;
volatile uint8_t g_voltage_monitor_enabled = 0U;
volatile uint8_t g_voltage_monitor_sample_valid = 0U;
volatile uint8_t g_voltage_monitor_adc_fault_active = 0U;
volatile uint8_t g_voltage_monitor_adc_error_consecutive = 0U;
volatile uint8_t g_voltage_monitor_adc_success_consecutive = 0U;

volatile uint32_t g_voltage_monitor_instant_fault_flags = 0U;
volatile uint32_t g_voltage_monitor_undervoltage_flags = 0U;
volatile uint32_t g_voltage_monitor_overvoltage_flags = 0U;
volatile uint32_t g_voltage_monitor_latched_fault_flags = 0U;

volatile uint32_t g_voltage_monitor_update_count = 0U;
volatile uint32_t g_voltage_monitor_adc_timeout_count = 0U;
volatile uint32_t g_voltage_monitor_fault_event_count = 0U;
volatile uint32_t g_voltage_monitor_recovery_event_count = 0U;
volatile uint32_t g_voltage_monitor_last_update_ms = 0U;
volatile uint32_t g_voltage_monitor_last_fault_ms = 0U;
volatile uint32_t g_voltage_monitor_task_call_count = 0U;

volatile voltage_monitor_channel_status_t g_voltage_monitor_channels[VOLTAGE_MONITOR_CHANNEL_COUNT];

volatile uint8_t g_voltage_monitor_test_enable = 0U;
volatile uint8_t g_voltage_monitor_test_force_adc_timeout = 0U;
volatile uint32_t g_voltage_monitor_test_mv[VOLTAGE_MONITOR_CHANNEL_COUNT] =
{
	18400UL,
	5000UL,
	3300UL,
	1800UL
};

static volatile uint8_t s_sampling_active = 0U;
static uint32_t s_sample_generation = 0U;
static uint32_t s_last_evaluated_generation = 0U;
static uint32_t s_monitor_enable_time_ms = 0U;
static uint32_t s_task_elapsed_ms = 0U;

static void voltage_monitor_saturating_increment_u32(volatile uint32_t *value)
{
	if (*value != VOLTAGE_MONITOR_UINT32_MAX_VALUE)
	{
		(*value)++;
	}
}

static void voltage_monitor_saturating_increment_u8(volatile uint8_t *value)
{
	if (*value != 0xFFU)
	{
		(*value)++;
	}
}

static uint8_t voltage_monitor_configuration_is_valid(void)
{
	uint32_t index;
	const voltage_monitor_channel_config_t *config;

	for (index = 0U; index < (uint32_t)VOLTAGE_MONITOR_CHANNEL_COUNT; index++)
	{
		config = &s_channel_config[index];

		if ((config->divider_numerator == 0U) || (config->divider_denominator == 0U) || (config->gain_ppm == 0U) || (config->minimum_mv >= config->maximum_mv) || (config->hysteresis_mv >= (config->maximum_mv - config->minimum_mv)))
		{
			return 0U;
		}
	}

	return 1U;
}

static void voltage_monitor_sort_samples(uint16_t *samples, uint32_t count)
{
	uint32_t index;
	uint32_t insert_index;
	uint16_t value;

	for (index = 1U; index < count; index++)
	{
		value = samples[index];
		insert_index = index;

		while ((insert_index > 0U) && (samples[insert_index - 1U] > value))
		{
			samples[insert_index] = samples[insert_index - 1U];
			insert_index--;
		}

		samples[insert_index] = value;
	}
}

static voltage_monitor_result_t voltage_monitor_adc_sample_once(uint8_t channel, uint16_t *raw_value)
{
	uint32_t timeout_loops = 0U;

	if (raw_value == (uint16_t *)0)
	{
		return VOLTAGE_MONITOR_RESULT_INVALID_ARGUMENT;
	}

	adc_regular_channel_config(VOLTAGE_ADC, 0U, channel, ADC_SAMPLETIME_239POINT5);
	adc_flag_clear(VOLTAGE_ADC, ADC_FLAG_EOC);
	adc_software_trigger_enable(VOLTAGE_ADC, ADC_REGULAR_CHANNEL);

	while (adc_flag_get(VOLTAGE_ADC, ADC_FLAG_EOC) == RESET)
	{
		timeout_loops++;

		if (timeout_loops >= VOLTAGE_MONITOR_ADC_TIMEOUT_LOOPS)
		{
			adc_flag_clear(VOLTAGE_ADC, ADC_FLAG_EOC);
			return VOLTAGE_MONITOR_RESULT_ADC_TIMEOUT;
		}
	}

	*raw_value = (uint16_t)adc_regular_data_read(VOLTAGE_ADC);
	adc_flag_clear(VOLTAGE_ADC, ADC_FLAG_EOC);

	return VOLTAGE_MONITOR_RESULT_OK;
}

static voltage_monitor_result_t voltage_monitor_adc_trimmed_average(uint8_t channel, uint16_t *average)
{
	uint16_t samples[VOLTAGE_MONITOR_SAMPLE_COUNT];
	uint16_t discarded_sample;
	uint32_t index;
	uint32_t sum = 0U;
	uint32_t retained_count = VOLTAGE_MONITOR_SAMPLE_COUNT - (2U * VOLTAGE_MONITOR_TRIM_COUNT);
	voltage_monitor_result_t result;

	if (average == (uint16_t *)0)
	{
		return VOLTAGE_MONITOR_RESULT_INVALID_ARGUMENT;
	}

	result = voltage_monitor_adc_sample_once(channel, &discarded_sample);
	if (result != VOLTAGE_MONITOR_RESULT_OK)
	{
		return result;
	}

	for (index = 0U; index < VOLTAGE_MONITOR_SAMPLE_COUNT; index++)
	{
		result = voltage_monitor_adc_sample_once(channel, &samples[index]);
		if (result != VOLTAGE_MONITOR_RESULT_OK)
		{
			return result;
		}
	}

	voltage_monitor_sort_samples(samples, VOLTAGE_MONITOR_SAMPLE_COUNT);

	for (index = VOLTAGE_MONITOR_TRIM_COUNT; index < (VOLTAGE_MONITOR_SAMPLE_COUNT - VOLTAGE_MONITOR_TRIM_COUNT); index++)
	{
		sum += (uint32_t)samples[index];
	}

	*average = (uint16_t)((sum + (retained_count / 2U)) / retained_count);
	return VOLTAGE_MONITOR_RESULT_OK;
}

static uint32_t voltage_monitor_raw_to_mv(uint16_t raw_value, const voltage_monitor_channel_config_t *config)
{
	uint64_t numerator;
	uint64_t denominator;
	uint64_t base_mv;
	uint64_t corrected_unsigned;
	int64_t corrected_signed;

	numerator = (uint64_t)raw_value * (uint64_t)VOLTAGE_MONITOR_ADC_REFERENCE_MV * (uint64_t)config->divider_numerator;
	denominator = (uint64_t)VOLTAGE_MONITOR_ADC_FULL_SCALE * (uint64_t)config->divider_denominator;

	base_mv = (numerator + (denominator / 2U)) / denominator;
	corrected_unsigned = ((base_mv * (uint64_t)config->gain_ppm) + ((uint64_t)VOLTAGE_MONITOR_GAIN_PPM_UNITY / 2U)) / (uint64_t)VOLTAGE_MONITOR_GAIN_PPM_UNITY;

	corrected_signed = (int64_t)corrected_unsigned + (int64_t)config->offset_mv;

	if (corrected_signed <= 0)
	{
		return 0U;
	}

	if ((uint64_t)corrected_signed > (uint64_t)VOLTAGE_MONITOR_UINT32_MAX_VALUE)
	{
		return VOLTAGE_MONITOR_UINT32_MAX_VALUE;
	}

	return (uint32_t)corrected_signed;
}

static void voltage_monitor_channel_runtime_clear(uint8_t clear_measurement)
{
	uint32_t index;

	for (index = 0U; index < (uint32_t)VOLTAGE_MONITOR_CHANNEL_COUNT; index++)
	{
		if (clear_measurement != 0U)
		{
			g_voltage_monitor_channels[index].raw_average = 0U;
			g_voltage_monitor_channels[index].millivolts = 0U;
		}

		g_voltage_monitor_channels[index].minimum_mv = s_channel_config[index].minimum_mv;
		g_voltage_monitor_channels[index].maximum_mv = s_channel_config[index].maximum_mv;
		g_voltage_monitor_channels[index].hysteresis_mv = s_channel_config[index].hysteresis_mv;
		g_voltage_monitor_channels[index].fault_bit = s_channel_config[index].fault_bit;

		g_voltage_monitor_channels[index].undervoltage_active = 0U;
		g_voltage_monitor_channels[index].overvoltage_active = 0U;
		g_voltage_monitor_channels[index].low_assert_count = 0U;
		g_voltage_monitor_channels[index].low_clear_count = 0U;
		g_voltage_monitor_channels[index].high_assert_count = 0U;
		g_voltage_monitor_channels[index].high_clear_count = 0U;
	}

	g_voltage_monitor_instant_fault_flags = 0U;
	g_voltage_monitor_undervoltage_flags = 0U;
	g_voltage_monitor_overvoltage_flags = 0U;
}

static void voltage_monitor_publish_measurements(const uint16_t raw_values[VOLTAGE_MONITOR_CHANNEL_COUNT], const uint32_t millivolts[VOLTAGE_MONITOR_CHANNEL_COUNT])
{
	uint32_t index;

	for (index = 0U; index < (uint32_t)VOLTAGE_MONITOR_CHANNEL_COUNT; index++)
	{
		g_voltage_monitor_channels[index].raw_average = raw_values[index];
		g_voltage_monitor_channels[index].millivolts = millivolts[index];
	}

	adc_orin_vdd_raw = raw_values[VOLTAGE_MONITOR_CHANNEL_ORIN_VDD];
	adc_5v_raw = raw_values[VOLTAGE_MONITOR_CHANNEL_5V];
	adc_3v3_raw = raw_values[VOLTAGE_MONITOR_CHANNEL_3V3];
	adc_1v8_raw = raw_values[VOLTAGE_MONITOR_CHANNEL_1V8];

	orin_vdd_mv = millivolts[VOLTAGE_MONITOR_CHANNEL_ORIN_VDD];
	system_5v_mv = millivolts[VOLTAGE_MONITOR_CHANNEL_5V];
	system_3v3_mv = millivolts[VOLTAGE_MONITOR_CHANNEL_3V3];
	system_1v8_mv = millivolts[VOLTAGE_MONITOR_CHANNEL_1V8];

	orin_vdd_voltage = (float)orin_vdd_mv / 1000.0f;
	system_5v_voltage = (float)system_5v_mv / 1000.0f;
	system_3v3_voltage = (float)system_3v3_mv / 1000.0f;
	system_1v8_voltage = (float)system_1v8_mv / 1000.0f;
}

static void voltage_monitor_adc_failure_record(voltage_monitor_result_t result)
{
	g_voltage_monitor_sample_valid = 0U;
	g_voltage_monitor_last_result = result;
	g_voltage_monitor_adc_success_consecutive = 0U;
	voltage_monitor_saturating_increment_u8(&g_voltage_monitor_adc_error_consecutive);

	if (result == VOLTAGE_MONITOR_RESULT_ADC_TIMEOUT)
	{
		voltage_monitor_saturating_increment_u32(&g_voltage_monitor_adc_timeout_count);
	}

	if (g_voltage_monitor_adc_error_consecutive >= VOLTAGE_MONITOR_ADC_ERROR_ASSERT_COUNT)
	{
		if (g_voltage_monitor_adc_fault_active == 0U)
		{
			voltage_monitor_saturating_increment_u32(&g_voltage_monitor_fault_event_count);
			g_voltage_monitor_last_fault_ms = systick_get_ms();
		}

		g_voltage_monitor_adc_fault_active = 1U;
		voltage_fault_flags |= VOLTAGE_FAULT_ADC;
		g_voltage_monitor_latched_fault_flags |= VOLTAGE_FAULT_ADC;
		g_voltage_monitor_state = VOLTAGE_MONITOR_STATE_ADC_ERROR;
	}
}

static void voltage_monitor_adc_success_record(void)
{
	g_voltage_monitor_sample_valid = 1U;
	g_voltage_monitor_last_result = VOLTAGE_MONITOR_RESULT_OK;
	g_voltage_monitor_adc_error_consecutive = 0U;

	if (g_voltage_monitor_adc_fault_active != 0U)
	{
		voltage_monitor_saturating_increment_u8(&g_voltage_monitor_adc_success_consecutive);

		if (g_voltage_monitor_adc_success_consecutive >= VOLTAGE_MONITOR_ADC_ERROR_CLEAR_COUNT)
		{
			g_voltage_monitor_adc_fault_active = 0U;
			g_voltage_monitor_adc_success_consecutive = 0U;
			voltage_fault_flags &= ~VOLTAGE_FAULT_ADC;
			voltage_monitor_saturating_increment_u32(&g_voltage_monitor_recovery_event_count);
		}
	}
	else
	{
		g_voltage_monitor_adc_success_consecutive = 0U;
	}
}

static void voltage_monitor_direction_update(uint8_t assert_condition, uint8_t clear_condition, volatile uint8_t *active, volatile uint8_t *assert_count, volatile uint8_t *clear_count)
{
	if (*active == 0U)
	{
		*clear_count = 0U;

		if (assert_condition != 0U)
		{
			voltage_monitor_saturating_increment_u8(assert_count);

			if (*assert_count >= VOLTAGE_MONITOR_FAULT_ASSERT_COUNT)
			{
				*active = 1U;
				*assert_count = 0U;
			}
		}
		else
		{
			*assert_count = 0U;
		}
	}
	else
	{
		*assert_count = 0U;

		if (clear_condition != 0U)
		{
			voltage_monitor_saturating_increment_u8(clear_count);

			if (*clear_count >= VOLTAGE_MONITOR_FAULT_CLEAR_COUNT)
			{
				*active = 0U;
				*clear_count = 0U;
			}
		}
		else
		{
			*clear_count = 0U;
		}
	}
}

static uint8_t voltage_monitor_any_assert_pending(void)
{
	uint32_t index;

	for (index = 0U; index < (uint32_t)VOLTAGE_MONITOR_CHANNEL_COUNT; index++)
	{
		if ((g_voltage_monitor_channels[index].low_assert_count != 0U) || (g_voltage_monitor_channels[index].high_assert_count != 0U))
		{
			return 1U;
		}
	}

	return 0U;
}

static uint8_t voltage_monitor_any_clear_pending(void)
{
	uint32_t index;

	for (index = 0U; index < (uint32_t)VOLTAGE_MONITOR_CHANNEL_COUNT; index++)
	{
		if ((g_voltage_monitor_channels[index].low_clear_count != 0U) || (g_voltage_monitor_channels[index].high_clear_count != 0U))
		{
			return 1U;
		}
	}

	return 0U;
}

void voltage_monitor_init(void)
{
	voltage_monitor_result_t result;

	g_voltage_monitor_initialized = 0U;
	g_voltage_monitor_enabled = 0U;
	g_voltage_monitor_sample_valid = 0U;
	g_voltage_monitor_adc_fault_active = 0U;
	g_voltage_monitor_adc_error_consecutive = 0U;
	g_voltage_monitor_adc_success_consecutive = 0U;
	g_voltage_monitor_state = VOLTAGE_MONITOR_STATE_UNINITIALIZED;
	g_voltage_monitor_init_result = VOLTAGE_MONITOR_RESULT_NOT_INITIALIZED;
	g_voltage_monitor_last_result = VOLTAGE_MONITOR_RESULT_NOT_INITIALIZED;

	voltage_fault_flags = VOLTAGE_FAULT_NONE;
	g_voltage_monitor_latched_fault_flags = VOLTAGE_FAULT_NONE;
	g_voltage_monitor_update_count = 0U;
	g_voltage_monitor_adc_timeout_count = 0U;
	g_voltage_monitor_fault_event_count = 0U;
	g_voltage_monitor_recovery_event_count = 0U;
	g_voltage_monitor_last_update_ms = 0U;
	g_voltage_monitor_last_fault_ms = 0U;
	g_voltage_monitor_task_call_count = 0U;

	s_sampling_active = 0U;
	s_sample_generation = 0U;
	s_last_evaluated_generation = 0U;
	s_task_elapsed_ms = 0U;

	voltage_monitor_channel_runtime_clear(1U);

	if (voltage_monitor_configuration_is_valid() == 0U)
	{
		g_voltage_monitor_init_result = VOLTAGE_MONITOR_RESULT_CONFIG_ERROR;
		g_voltage_monitor_last_result = VOLTAGE_MONITOR_RESULT_CONFIG_ERROR;
		g_voltage_monitor_state = VOLTAGE_MONITOR_STATE_CONFIG_ERROR;
		return;
	}

	rcu_periph_clock_enable(VOLTAGE_ADC_GPIO_RCU);
	rcu_periph_clock_enable(VOLTAGE_ADC_RCU);
	rcu_adc_clock_config(RCU_CKADC_CKAPB2_DIV6);

	gpio_init(ORIN_VDD_SENSE_PORT, GPIO_MODE_AIN, GPIO_OSPEED_10MHZ, ORIN_VDD_SENSE_PIN);
	gpio_init(SYSTEM_5V_SENSE_PORT, GPIO_MODE_AIN, GPIO_OSPEED_10MHZ, SYSTEM_5V_SENSE_PIN);
	gpio_init(SYSTEM_3V3_SENSE_PORT, GPIO_MODE_AIN, GPIO_OSPEED_10MHZ, SYSTEM_3V3_SENSE_PIN);
	gpio_init(SYSTEM_1V8_SENSE_PORT, GPIO_MODE_AIN, GPIO_OSPEED_10MHZ, SYSTEM_1V8_SENSE_PIN);

	adc_mode_config(ADC_MODE_FREE);
	adc_special_function_config(VOLTAGE_ADC, ADC_SCAN_MODE, DISABLE);
	adc_special_function_config(VOLTAGE_ADC, ADC_CONTINUOUS_MODE, DISABLE);
	adc_data_alignment_config(VOLTAGE_ADC, ADC_DATAALIGN_RIGHT);
	adc_channel_length_config(VOLTAGE_ADC, ADC_REGULAR_CHANNEL, 1U);
	adc_external_trigger_source_config(VOLTAGE_ADC, ADC_REGULAR_CHANNEL, ADC0_1_2_EXTTRIG_REGULAR_NONE);
	adc_external_trigger_config(VOLTAGE_ADC, ADC_REGULAR_CHANNEL, ENABLE);

	adc_enable(VOLTAGE_ADC);
	delay_1ms(1U);
	adc_calibration_enable(VOLTAGE_ADC);

	g_voltage_monitor_initialized = 1U;
	g_voltage_monitor_enabled = 1U;
	g_voltage_monitor_state = VOLTAGE_MONITOR_STATE_STARTUP_GRACE;
	s_monitor_enable_time_ms = systick_get_ms();

	result = voltage_monitor_run_cycle();
	g_voltage_monitor_init_result = result;
}

voltage_monitor_result_t voltage_monitor_update_checked(void)
{
	uint16_t raw_values[VOLTAGE_MONITOR_CHANNEL_COUNT];
	uint32_t millivolts[VOLTAGE_MONITOR_CHANNEL_COUNT];
	uint32_t index;
	voltage_monitor_result_t result;

	if (g_voltage_monitor_initialized == 0U)
	{
		g_voltage_monitor_last_result = VOLTAGE_MONITOR_RESULT_NOT_INITIALIZED;
		return VOLTAGE_MONITOR_RESULT_NOT_INITIALIZED;
	}

	if (s_sampling_active != 0U)
	{
		g_voltage_monitor_last_result = VOLTAGE_MONITOR_RESULT_BUSY;
		return VOLTAGE_MONITOR_RESULT_BUSY;
	}

	s_sampling_active = 1U;

#if (VOLTAGE_MONITOR_ENABLE_SOFTWARE_TEST == 1U)
	if ((g_voltage_monitor_test_enable == VOLTAGE_MONITOR_TEST_ENABLE_KEY) && (g_voltage_monitor_test_force_adc_timeout != 0U))
	{
		s_sampling_active = 0U;
		voltage_monitor_adc_failure_record(VOLTAGE_MONITOR_RESULT_ADC_TIMEOUT);
		return VOLTAGE_MONITOR_RESULT_ADC_TIMEOUT;
	}

	if (g_voltage_monitor_test_enable == VOLTAGE_MONITOR_TEST_ENABLE_KEY)
	{
		for (index = 0U; index < (uint32_t)VOLTAGE_MONITOR_CHANNEL_COUNT; index++)
		{
			raw_values[index] = g_voltage_monitor_channels[index].raw_average;
			millivolts[index] = g_voltage_monitor_test_mv[index];
		}
	}
	else
#endif
	{
		for (index = 0U; index < (uint32_t)VOLTAGE_MONITOR_CHANNEL_COUNT; index++)
		{
			result = voltage_monitor_adc_trimmed_average(s_channel_config[index].adc_channel, &raw_values[index]);

			if (result != VOLTAGE_MONITOR_RESULT_OK)
			{
				s_sampling_active = 0U;
				voltage_monitor_adc_failure_record(result);
				return result;
			}

			millivolts[index] = voltage_monitor_raw_to_mv(raw_values[index], &s_channel_config[index]);
		}
	}

	voltage_monitor_publish_measurements(raw_values, millivolts);
	s_sampling_active = 0U;

	voltage_monitor_adc_success_record();
	voltage_monitor_saturating_increment_u32(&g_voltage_monitor_update_count);
	g_voltage_monitor_last_update_ms = systick_get_ms();

	s_sample_generation++;
	if (s_sample_generation == 0U)
	{
		s_sample_generation = 1U;
	}

	return VOLTAGE_MONITOR_RESULT_OK;
}

void voltage_monitor_update(void)
{
	(void)voltage_monitor_update_checked();
}

void voltage_monitor_fault_check(void)
{
	uint32_t index;
	uint32_t elapsed_ms;
	uint32_t old_rail_flags;
	uint32_t new_rail_flags = 0U;
	uint32_t new_low_flags = 0U;
	uint32_t new_high_flags = 0U;
	uint32_t instant_flags = 0U;
	uint32_t newly_asserted;
	uint32_t newly_cleared;
	uint32_t value_mv;
	uint32_t low_clear_threshold;
	uint32_t high_clear_threshold;
	voltage_monitor_channel_status_t volatile *channel;
	const voltage_monitor_channel_config_t *config;

	if (g_voltage_monitor_initialized == 0U)
	{
		return;
	}

	if (g_voltage_monitor_enabled == 0U)
	{
		g_voltage_monitor_state = VOLTAGE_MONITOR_STATE_SUSPENDED;
		return;
	}

	if (s_sample_generation == s_last_evaluated_generation)
	{
		return;
	}

	s_last_evaluated_generation = s_sample_generation;
	old_rail_flags = voltage_fault_flags & VOLTAGE_FAULT_RAIL_MASK;

	for (index = 0U; index < (uint32_t)VOLTAGE_MONITOR_CHANNEL_COUNT; index++)
	{
		channel = &g_voltage_monitor_channels[index];
		config = &s_channel_config[index];
		value_mv = channel->millivolts;

		if ((value_mv < config->minimum_mv) || (value_mv > config->maximum_mv))
		{
			instant_flags |= config->fault_bit;
		}
	}

	g_voltage_monitor_instant_fault_flags = instant_flags;
	elapsed_ms = (uint32_t)(systick_get_ms() - s_monitor_enable_time_ms);

	if (elapsed_ms < VOLTAGE_MONITOR_STARTUP_GRACE_MS)
	{
		voltage_monitor_channel_runtime_clear(0U);
		g_voltage_monitor_instant_fault_flags = instant_flags;
		voltage_fault_flags = (g_voltage_monitor_adc_fault_active != 0U) ? VOLTAGE_FAULT_ADC : VOLTAGE_FAULT_NONE;

		g_voltage_monitor_state = (g_voltage_monitor_adc_fault_active != 0U) ? VOLTAGE_MONITOR_STATE_ADC_ERROR : VOLTAGE_MONITOR_STATE_STARTUP_GRACE;
		return;
	}

	for (index = 0U; index < (uint32_t)VOLTAGE_MONITOR_CHANNEL_COUNT; index++)
	{
		channel = &g_voltage_monitor_channels[index];
		config = &s_channel_config[index];
		value_mv = channel->millivolts;
		low_clear_threshold = config->minimum_mv + config->hysteresis_mv;
		high_clear_threshold = config->maximum_mv - config->hysteresis_mv;

		voltage_monitor_direction_update((uint8_t)((value_mv < config->minimum_mv) ? 1U : 0U), (uint8_t)((value_mv >= low_clear_threshold) ? 1U : 0U), &channel->undervoltage_active, &channel->low_assert_count, &channel->low_clear_count);

		voltage_monitor_direction_update((uint8_t)((value_mv > config->maximum_mv) ? 1U : 0U), (uint8_t)((value_mv <= high_clear_threshold) ? 1U : 0U), &channel->overvoltage_active, &channel->high_assert_count, &channel->high_clear_count);

		if (channel->undervoltage_active != 0U)
		{
			new_low_flags |= config->fault_bit;
		}

		if (channel->overvoltage_active != 0U)
		{
			new_high_flags |= config->fault_bit;
		}
	}

	new_rail_flags = new_low_flags | new_high_flags;
	newly_asserted = new_rail_flags & ~old_rail_flags;
	newly_cleared = old_rail_flags & ~new_rail_flags;

	g_voltage_monitor_undervoltage_flags = new_low_flags;
	g_voltage_monitor_overvoltage_flags = new_high_flags;
	voltage_fault_flags = new_rail_flags | ((g_voltage_monitor_adc_fault_active != 0U) ? VOLTAGE_FAULT_ADC : VOLTAGE_FAULT_NONE);

	if (newly_asserted != 0U)
	{
		g_voltage_monitor_latched_fault_flags |= newly_asserted;
		voltage_monitor_saturating_increment_u32(&g_voltage_monitor_fault_event_count);
		g_voltage_monitor_last_fault_ms = systick_get_ms();
	}

	if (newly_cleared != 0U)
	{
		voltage_monitor_saturating_increment_u32(&g_voltage_monitor_recovery_event_count);
	}

	if (g_voltage_monitor_adc_fault_active != 0U)
	{
		g_voltage_monitor_state = VOLTAGE_MONITOR_STATE_ADC_ERROR;
	}
	else if (new_rail_flags != 0U)
	{
		g_voltage_monitor_state = (voltage_monitor_any_clear_pending() != 0U) ? VOLTAGE_MONITOR_STATE_RECOVERY_PENDING : VOLTAGE_MONITOR_STATE_FAULT_ACTIVE;
	}
	else if (voltage_monitor_any_assert_pending() != 0U)
	{
		g_voltage_monitor_state = VOLTAGE_MONITOR_STATE_FAULT_PENDING;
	}
	else
	{
		g_voltage_monitor_state = VOLTAGE_MONITOR_STATE_NORMAL;
	}
}

voltage_monitor_result_t voltage_monitor_run_cycle(void)
{
	voltage_monitor_result_t result = voltage_monitor_update_checked();

	if (result == VOLTAGE_MONITOR_RESULT_OK)
	{
		voltage_monitor_fault_check();
	}

	return result;
}

void voltage_monitor_task_1ms(void)
{
	if (g_voltage_monitor_initialized == 0U)
	{
		return;
	}

	voltage_monitor_saturating_increment_u32(&g_voltage_monitor_task_call_count);

	if (g_voltage_monitor_enabled == 0U)
	{
		s_task_elapsed_ms = 0U;
		return;
	}

	s_task_elapsed_ms++;

	if (s_task_elapsed_ms >= VOLTAGE_MONITOR_UPDATE_PERIOD_MS)
	{
		s_task_elapsed_ms = 0U;
		(void)voltage_monitor_run_cycle();
	}
}

void voltage_monitor_set_enabled(uint8_t enabled)
{
	uint8_t requested_enabled = (enabled != 0U) ? 1U : 0U;

	if (g_voltage_monitor_initialized == 0U)
	{
		g_voltage_monitor_last_result = VOLTAGE_MONITOR_RESULT_NOT_INITIALIZED;
		return;
	}

	if (requested_enabled == g_voltage_monitor_enabled)
	{
		return;
	}

	voltage_monitor_channel_runtime_clear(0U);
	voltage_fault_flags = VOLTAGE_FAULT_NONE;
	g_voltage_monitor_sample_valid = 0U;
	g_voltage_monitor_adc_fault_active = 0U;
	g_voltage_monitor_adc_error_consecutive = 0U;
	g_voltage_monitor_adc_success_consecutive = 0U;
	s_task_elapsed_ms = 0U;
	s_last_evaluated_generation = s_sample_generation;

	g_voltage_monitor_enabled = requested_enabled;

	if (requested_enabled != 0U)
	{
		s_monitor_enable_time_ms = systick_get_ms();
		g_voltage_monitor_state = VOLTAGE_MONITOR_STATE_STARTUP_GRACE;
	}
	else
	{
		g_voltage_monitor_state = VOLTAGE_MONITOR_STATE_SUSPENDED;
	}
}

uint8_t voltage_monitor_is_initialized(void)
{
	return g_voltage_monitor_initialized;
}

uint8_t voltage_monitor_is_healthy(void)
{
	if ((g_voltage_monitor_initialized != 0U) && (g_voltage_monitor_enabled != 0U) && (g_voltage_monitor_sample_valid != 0U) && (voltage_fault_flags == VOLTAGE_FAULT_NONE) && (g_voltage_monitor_state == VOLTAGE_MONITOR_STATE_NORMAL))
	{
		return 1U;
	}

	return 0U;
}

voltage_monitor_state_t voltage_monitor_get_state(void)
{
	return g_voltage_monitor_state;
}

voltage_monitor_result_t voltage_monitor_get_last_result(void)
{
	return g_voltage_monitor_last_result;
}

uint32_t voltage_monitor_get_fault_flags(void)
{
	return voltage_fault_flags;
}

void voltage_monitor_clear_latched_faults(void)
{
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	g_voltage_monitor_latched_fault_flags = voltage_fault_flags;

	if (primask == 0U)
	{
		__enable_irq();
	}
}

void voltage_monitor_get_status(voltage_monitor_status_t *status)
{
	uint32_t index;
	uint32_t primask;

	if (status == (voltage_monitor_status_t *)0)
	{
		g_voltage_monitor_last_result = VOLTAGE_MONITOR_RESULT_INVALID_ARGUMENT;
		return;
	}

	primask = __get_PRIMASK();
	__disable_irq();

	status->state = g_voltage_monitor_state;
	status->init_result = g_voltage_monitor_init_result;
	status->last_result = g_voltage_monitor_last_result;
	status->initialized = g_voltage_monitor_initialized;
	status->monitoring_enabled = g_voltage_monitor_enabled;
	status->sample_valid = g_voltage_monitor_sample_valid;
	status->adc_fault_active = g_voltage_monitor_adc_fault_active;
	status->active_fault_flags = voltage_fault_flags;
	status->instantaneous_fault_flags = g_voltage_monitor_instant_fault_flags;
	status->undervoltage_flags = g_voltage_monitor_undervoltage_flags;
	status->overvoltage_flags = g_voltage_monitor_overvoltage_flags;
	status->latched_fault_flags = g_voltage_monitor_latched_fault_flags;
	status->update_count = g_voltage_monitor_update_count;
	status->adc_timeout_count = g_voltage_monitor_adc_timeout_count;
	status->fault_event_count = g_voltage_monitor_fault_event_count;
	status->recovery_event_count = g_voltage_monitor_recovery_event_count;
	status->last_update_ms = g_voltage_monitor_last_update_ms;
	status->last_fault_ms = g_voltage_monitor_last_fault_ms;

	for (index = 0U; index < (uint32_t)VOLTAGE_MONITOR_CHANNEL_COUNT; index++)
	{
		status->channels[index] = g_voltage_monitor_channels[index];
	}

	if (primask == 0U)
	{
		__enable_irq();
	}
}

uint8_t voltage_monitor_get_channel_status(voltage_monitor_channel_t channel, voltage_monitor_channel_status_t *status)
{
	uint32_t primask;

	if ((status == (voltage_monitor_channel_status_t *)0) || ((uint32_t)channel >= (uint32_t)VOLTAGE_MONITOR_CHANNEL_COUNT))
	{
		g_voltage_monitor_last_result = VOLTAGE_MONITOR_RESULT_INVALID_ARGUMENT;
		return 0U;
	}

	primask = __get_PRIMASK();
	__disable_irq();
	*status = g_voltage_monitor_channels[(uint32_t)channel];

	if (primask == 0U)
	{
		__enable_irq();
	}

	return 1U;
}
