#ifndef VOLTAGE_MONITOR_H
#define VOLTAGE_MONITOR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VOLTAGE_MONITOR_VERSION_MAJOR    (1U)
#define VOLTAGE_MONITOR_VERSION_MINOR    (0U)
#define VOLTAGE_MONITOR_VERSION_PATCH    (0U)


#define VOLTAGE_FAULT_NONE    (0x00000000UL)
#define VOLTAGE_FAULT_ORIN_VDD    (0x00000001UL)
#define VOLTAGE_FAULT_5V    (0x00000002UL)
#define VOLTAGE_FAULT_3V3    (0x00000004UL)
#define VOLTAGE_FAULT_1V8    (0x00000008UL)
#define VOLTAGE_FAULT_RAIL_MASK    (0x0000000FUL)
#define VOLTAGE_FAULT_ADC    (0x80000000UL)


#ifndef VOLTAGE_MONITOR_ADC_REFERENCE_MV
#define VOLTAGE_MONITOR_ADC_REFERENCE_MV    (3300UL)
#endif

#ifndef VOLTAGE_MONITOR_ADC_FULL_SCALE
#define VOLTAGE_MONITOR_ADC_FULL_SCALE    (4095UL)
#endif

#ifndef VOLTAGE_MONITOR_SAMPLE_COUNT
#define VOLTAGE_MONITOR_SAMPLE_COUNT    (16U)
#endif

#ifndef VOLTAGE_MONITOR_TRIM_COUNT
#define VOLTAGE_MONITOR_TRIM_COUNT    (2U)
#endif

#ifndef VOLTAGE_MONITOR_ADC_TIMEOUT_LOOPS
#define VOLTAGE_MONITOR_ADC_TIMEOUT_LOOPS    (100000UL)
#endif

#ifndef VOLTAGE_MONITOR_UPDATE_PERIOD_MS
#define VOLTAGE_MONITOR_UPDATE_PERIOD_MS    (200UL)
#endif

#ifndef VOLTAGE_MONITOR_STARTUP_GRACE_MS
#define VOLTAGE_MONITOR_STARTUP_GRACE_MS    (500UL)
#endif

#ifndef VOLTAGE_MONITOR_FAULT_ASSERT_COUNT
#define VOLTAGE_MONITOR_FAULT_ASSERT_COUNT    (3U)
#endif

#ifndef VOLTAGE_MONITOR_FAULT_CLEAR_COUNT
#define VOLTAGE_MONITOR_FAULT_CLEAR_COUNT    (5U)
#endif

#ifndef VOLTAGE_MONITOR_ADC_ERROR_ASSERT_COUNT
#define VOLTAGE_MONITOR_ADC_ERROR_ASSERT_COUNT    (3U)
#endif

#ifndef VOLTAGE_MONITOR_ADC_ERROR_CLEAR_COUNT
#define VOLTAGE_MONITOR_ADC_ERROR_CLEAR_COUNT    (3U)
#endif

#ifndef VOLTAGE_MONITOR_ENABLE_SOFTWARE_TEST
#define VOLTAGE_MONITOR_ENABLE_SOFTWARE_TEST    (1U)
#endif

#define VOLTAGE_MONITOR_TEST_ENABLE_KEY    (0xA5U)


#ifndef VOLTAGE_MONITOR_ORIN_DIVIDER_NUM
#define VOLTAGE_MONITOR_ORIN_DIVIDER_NUM    (9450UL)
#endif
#ifndef VOLTAGE_MONITOR_ORIN_DIVIDER_DEN
#define VOLTAGE_MONITOR_ORIN_DIVIDER_DEN    (1000UL)
#endif

#ifndef VOLTAGE_MONITOR_5V_DIVIDER_NUM
#define VOLTAGE_MONITOR_5V_DIVIDER_NUM    (2500UL)
#endif
#ifndef VOLTAGE_MONITOR_5V_DIVIDER_DEN
#define VOLTAGE_MONITOR_5V_DIVIDER_DEN    (1000UL)
#endif

#ifndef VOLTAGE_MONITOR_3V3_DIVIDER_NUM
#define VOLTAGE_MONITOR_3V3_DIVIDER_NUM    (1649UL)
#endif
#ifndef VOLTAGE_MONITOR_3V3_DIVIDER_DEN
#define VOLTAGE_MONITOR_3V3_DIVIDER_DEN    (1000UL)
#endif

#ifndef VOLTAGE_MONITOR_1V8_DIVIDER_NUM
#define VOLTAGE_MONITOR_1V8_DIVIDER_NUM    (1000UL)
#endif
#ifndef VOLTAGE_MONITOR_1V8_DIVIDER_DEN
#define VOLTAGE_MONITOR_1V8_DIVIDER_DEN    (1000UL)
#endif


#ifndef VOLTAGE_MONITOR_ORIN_GAIN_PPM
#define VOLTAGE_MONITOR_ORIN_GAIN_PPM    (1000000UL)
#endif
#ifndef VOLTAGE_MONITOR_ORIN_OFFSET_MV
#define VOLTAGE_MONITOR_ORIN_OFFSET_MV    (0L)
#endif

#ifndef VOLTAGE_MONITOR_5V_GAIN_PPM
#define VOLTAGE_MONITOR_5V_GAIN_PPM    (1000000UL)
#endif
#ifndef VOLTAGE_MONITOR_5V_OFFSET_MV
#define VOLTAGE_MONITOR_5V_OFFSET_MV    (0L)
#endif

#ifndef VOLTAGE_MONITOR_3V3_GAIN_PPM
#define VOLTAGE_MONITOR_3V3_GAIN_PPM    (1000000UL)
#endif
#ifndef VOLTAGE_MONITOR_3V3_OFFSET_MV
#define VOLTAGE_MONITOR_3V3_OFFSET_MV    (0L)
#endif

#ifndef VOLTAGE_MONITOR_1V8_GAIN_PPM
#define VOLTAGE_MONITOR_1V8_GAIN_PPM    (1000000UL)
#endif
#ifndef VOLTAGE_MONITOR_1V8_OFFSET_MV
#define VOLTAGE_MONITOR_1V8_OFFSET_MV    (0L)
#endif


#ifndef VOLTAGE_MONITOR_ORIN_MIN_MV
#define VOLTAGE_MONITOR_ORIN_MIN_MV    (17480UL)
#endif
#ifndef VOLTAGE_MONITOR_ORIN_MAX_MV
#define VOLTAGE_MONITOR_ORIN_MAX_MV    (19320UL)
#endif
#ifndef VOLTAGE_MONITOR_ORIN_HYST_MV
#define VOLTAGE_MONITOR_ORIN_HYST_MV    (200UL)
#endif

#ifndef VOLTAGE_MONITOR_5V_MIN_MV
#define VOLTAGE_MONITOR_5V_MIN_MV    (4750UL)
#endif
#ifndef VOLTAGE_MONITOR_5V_MAX_MV
#define VOLTAGE_MONITOR_5V_MAX_MV    (5250UL)
#endif
#ifndef VOLTAGE_MONITOR_5V_HYST_MV
#define VOLTAGE_MONITOR_5V_HYST_MV    (50UL)
#endif

#ifndef VOLTAGE_MONITOR_3V3_MIN_MV
#define VOLTAGE_MONITOR_3V3_MIN_MV    (3140UL)
#endif
#ifndef VOLTAGE_MONITOR_3V3_MAX_MV
#define VOLTAGE_MONITOR_3V3_MAX_MV    (3470UL)
#endif
#ifndef VOLTAGE_MONITOR_3V3_HYST_MV
#define VOLTAGE_MONITOR_3V3_HYST_MV    (40UL)
#endif

#ifndef VOLTAGE_MONITOR_1V8_MIN_MV
#define VOLTAGE_MONITOR_1V8_MIN_MV    (1710UL)
#endif
#ifndef VOLTAGE_MONITOR_1V8_MAX_MV
#define VOLTAGE_MONITOR_1V8_MAX_MV    (1890UL)
#endif
#ifndef VOLTAGE_MONITOR_1V8_HYST_MV
#define VOLTAGE_MONITOR_1V8_HYST_MV    (30UL)
#endif

typedef enum
{
	VOLTAGE_MONITOR_CHANNEL_ORIN_VDD = 0,
	VOLTAGE_MONITOR_CHANNEL_5V,
	VOLTAGE_MONITOR_CHANNEL_3V3,
	VOLTAGE_MONITOR_CHANNEL_1V8,
	VOLTAGE_MONITOR_CHANNEL_COUNT
} voltage_monitor_channel_t;

typedef enum
{
	VOLTAGE_MONITOR_STATE_UNINITIALIZED = 0,
	VOLTAGE_MONITOR_STATE_STARTUP_GRACE,
	VOLTAGE_MONITOR_STATE_NORMAL,
	VOLTAGE_MONITOR_STATE_FAULT_PENDING,
	VOLTAGE_MONITOR_STATE_FAULT_ACTIVE,
	VOLTAGE_MONITOR_STATE_RECOVERY_PENDING,
	VOLTAGE_MONITOR_STATE_SUSPENDED,
	VOLTAGE_MONITOR_STATE_ADC_ERROR,
	VOLTAGE_MONITOR_STATE_CONFIG_ERROR
} voltage_monitor_state_t;

typedef enum
{
	VOLTAGE_MONITOR_RESULT_OK = 0,
	VOLTAGE_MONITOR_RESULT_NOT_INITIALIZED,
	VOLTAGE_MONITOR_RESULT_INVALID_ARGUMENT,
	VOLTAGE_MONITOR_RESULT_BUSY,
	VOLTAGE_MONITOR_RESULT_CONFIG_ERROR,
	VOLTAGE_MONITOR_RESULT_ADC_TIMEOUT
} voltage_monitor_result_t;

typedef struct
{
	uint16_t raw_average;
	uint32_t millivolts;
	uint32_t minimum_mv;
	uint32_t maximum_mv;
	uint32_t hysteresis_mv;
	uint32_t fault_bit;

	uint8_t undervoltage_active;
	uint8_t overvoltage_active;
	uint8_t low_assert_count;
	uint8_t low_clear_count;
	uint8_t high_assert_count;
	uint8_t high_clear_count;
} voltage_monitor_channel_status_t;

typedef struct
{
	voltage_monitor_state_t state;
	voltage_monitor_result_t init_result;
	voltage_monitor_result_t last_result;

	uint8_t initialized;
	uint8_t monitoring_enabled;
	uint8_t sample_valid;
	uint8_t adc_fault_active;

	uint32_t active_fault_flags;
	uint32_t instantaneous_fault_flags;
	uint32_t undervoltage_flags;
	uint32_t overvoltage_flags;
	uint32_t latched_fault_flags;

	uint32_t update_count;
	uint32_t adc_timeout_count;
	uint32_t fault_event_count;
	uint32_t recovery_event_count;
	uint32_t last_update_ms;
	uint32_t last_fault_ms;

	voltage_monitor_channel_status_t channels[VOLTAGE_MONITOR_CHANNEL_COUNT];
} voltage_monitor_status_t;


extern volatile uint16_t adc_orin_vdd_raw;
extern volatile uint16_t adc_5v_raw;
extern volatile uint16_t adc_3v3_raw;
extern volatile uint16_t adc_1v8_raw;

extern volatile float orin_vdd_voltage;
extern volatile float system_5v_voltage;
extern volatile float system_3v3_voltage;
extern volatile float system_1v8_voltage;

extern volatile uint32_t voltage_fault_flags;

extern volatile uint32_t orin_vdd_mv;
extern volatile uint32_t system_5v_mv;
extern volatile uint32_t system_3v3_mv;
extern volatile uint32_t system_1v8_mv;


extern volatile voltage_monitor_state_t g_voltage_monitor_state;
extern volatile voltage_monitor_result_t g_voltage_monitor_init_result;
extern volatile voltage_monitor_result_t g_voltage_monitor_last_result;

extern volatile uint8_t g_voltage_monitor_initialized;
extern volatile uint8_t g_voltage_monitor_enabled;
extern volatile uint8_t g_voltage_monitor_sample_valid;
extern volatile uint8_t g_voltage_monitor_adc_fault_active;
extern volatile uint8_t g_voltage_monitor_adc_error_consecutive;
extern volatile uint8_t g_voltage_monitor_adc_success_consecutive;

extern volatile uint32_t g_voltage_monitor_instant_fault_flags;
extern volatile uint32_t g_voltage_monitor_undervoltage_flags;
extern volatile uint32_t g_voltage_monitor_overvoltage_flags;
extern volatile uint32_t g_voltage_monitor_latched_fault_flags;

extern volatile uint32_t g_voltage_monitor_update_count;
extern volatile uint32_t g_voltage_monitor_adc_timeout_count;
extern volatile uint32_t g_voltage_monitor_fault_event_count;
extern volatile uint32_t g_voltage_monitor_recovery_event_count;
extern volatile uint32_t g_voltage_monitor_last_update_ms;
extern volatile uint32_t g_voltage_monitor_last_fault_ms;
extern volatile uint32_t g_voltage_monitor_task_call_count;

extern volatile voltage_monitor_channel_status_t g_voltage_monitor_channels[VOLTAGE_MONITOR_CHANNEL_COUNT];


extern volatile uint8_t g_voltage_monitor_test_enable;
extern volatile uint8_t g_voltage_monitor_test_force_adc_timeout;
extern volatile uint32_t g_voltage_monitor_test_mv[VOLTAGE_MONITOR_CHANNEL_COUNT];


void voltage_monitor_init(void);

void voltage_monitor_update(void);
void voltage_monitor_fault_check(void);

voltage_monitor_result_t voltage_monitor_update_checked(void);
voltage_monitor_result_t voltage_monitor_run_cycle(void);

void voltage_monitor_task_1ms(void);
void voltage_monitor_set_enabled(uint8_t enabled);

uint8_t voltage_monitor_is_initialized(void);
uint8_t voltage_monitor_is_healthy(void);
voltage_monitor_state_t voltage_monitor_get_state(void);
voltage_monitor_result_t voltage_monitor_get_last_result(void);
uint32_t voltage_monitor_get_fault_flags(void);

void voltage_monitor_clear_latched_faults(void);
void voltage_monitor_get_status(voltage_monitor_status_t *status);
uint8_t voltage_monitor_get_channel_status(voltage_monitor_channel_t channel, voltage_monitor_channel_status_t *status);

#ifdef __cplusplus
}
#endif

#endif 
