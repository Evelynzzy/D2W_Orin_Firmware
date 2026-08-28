#ifndef RTC_CALENDAR_H
#define RTC_CALENDAR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


#define RTC_CALENDAR_DEVICE_ADDRESS_7BIT    (0x51U)
#define RTC_CALENDAR_DEVICE_ADDRESS_8BIT    (0xA2U)

#ifndef RTC_CALENDAR_I2C_SPEED_HZ
#define RTC_CALENDAR_I2C_SPEED_HZ    (100000U)
#endif

#ifndef RTC_CALENDAR_I2C_TIMEOUT_MS
#define RTC_CALENDAR_I2C_TIMEOUT_MS    (20U)
#endif


#ifndef RTC_CALENDAR_I2C_TIMEOUT_SPINS
#define RTC_CALENDAR_I2C_TIMEOUT_SPINS    (200000UL)
#endif

#ifndef RTC_CALENDAR_UPDATE_PERIOD_MS
#define RTC_CALENDAR_UPDATE_PERIOD_MS    (1000U)
#endif


#ifndef RTC_CALENDAR_CENTURY_BIT_FOR_2000_2099
#define RTC_CALENDAR_CENTURY_BIT_FOR_2000_2099    (0U)
#endif

typedef enum
{
	RTC_CALENDAR_RESULT_OK = 0,
	RTC_CALENDAR_RESULT_NOT_INITIALIZED,
	RTC_CALENDAR_RESULT_INVALID_ARGUMENT,
	RTC_CALENDAR_RESULT_DEVICE_NOT_FOUND,
	RTC_CALENDAR_RESULT_BUS_STUCK,
	RTC_CALENDAR_RESULT_START_TIMEOUT,
	RTC_CALENDAR_RESULT_ADDRESS_TIMEOUT,
	RTC_CALENDAR_RESULT_TRANSFER_TIMEOUT,
	RTC_CALENDAR_RESULT_STOP_TIMEOUT,
	RTC_CALENDAR_RESULT_BUS_ERROR,
	RTC_CALENDAR_RESULT_DATA_INVALID,
	RTC_CALENDAR_RESULT_VOLTAGE_LOW,
	RTC_CALENDAR_RESULT_CLOCK_STOPPED,
	RTC_CALENDAR_RESULT_VERIFY_FAILED
} rtc_calendar_result_t;

typedef enum
{
	RTC_CALENDAR_STATE_UNINITIALIZED = 0,
	RTC_CALENDAR_STATE_READY,
	RTC_CALENDAR_STATE_TIME_INVALID,
	RTC_CALENDAR_STATE_CLOCK_STOPPED,
	RTC_CALENDAR_STATE_COMMUNICATION_FAULT
} rtc_calendar_state_t;


typedef struct
{
	uint16_t year;             
	uint8_t month;            
	uint8_t day;               
	uint8_t weekday;           
	uint8_t hour;              
	uint8_t minute;            
	uint8_t second;            
	uint8_t voltage_low;       
	uint8_t century_flag;      
} rtc_calendar_datetime_t;

typedef struct
{
	rtc_calendar_state_t state;
	rtc_calendar_result_t init_result;
	rtc_calendar_result_t last_result;
	rtc_calendar_datetime_t datetime;

	uint8_t initialized;
	uint8_t device_present;
	uint8_t time_valid;
	uint8_t voltage_low;
	uint8_t clock_running;

	uint32_t refresh_count;
	uint32_t set_count;
	uint32_t error_count;
	uint32_t consecutive_error_count;
	uint32_t bus_recovery_count;
	uint32_t task_call_count;
	uint32_t last_update_ms;
	uint32_t last_success_ms;
	uint32_t last_error_ms;
} rtc_calendar_status_t;



extern volatile rtc_calendar_state_t g_rtc_calendar_state;
extern volatile rtc_calendar_result_t g_rtc_calendar_init_result;
extern volatile rtc_calendar_result_t g_rtc_calendar_last_result;
extern volatile rtc_calendar_datetime_t g_rtc_calendar_datetime;

extern volatile uint8_t g_rtc_calendar_initialized;
extern volatile uint8_t g_rtc_calendar_device_present;
extern volatile uint8_t g_rtc_calendar_time_valid;
extern volatile uint8_t g_rtc_calendar_voltage_low;
extern volatile uint8_t g_rtc_calendar_clock_running;

extern volatile uint32_t g_rtc_calendar_refresh_count;
extern volatile uint32_t g_rtc_calendar_set_count;
extern volatile uint32_t g_rtc_calendar_error_count;
extern volatile uint32_t g_rtc_calendar_consecutive_error_count;
extern volatile uint32_t g_rtc_calendar_bus_recovery_count;
extern volatile uint32_t g_rtc_calendar_task_call_count;
extern volatile uint32_t g_rtc_calendar_last_update_ms;
extern volatile uint32_t g_rtc_calendar_last_success_ms;
extern volatile uint32_t g_rtc_calendar_last_error_ms;


rtc_calendar_result_t rtc_calendar_init(void);


void rtc_calendar_task_1ms(void);
rtc_calendar_result_t rtc_calendar_refresh_now(void);
rtc_calendar_result_t rtc_calendar_get_datetime(rtc_calendar_datetime_t *datetime);
rtc_calendar_result_t rtc_calendar_set_datetime(const rtc_calendar_datetime_t *datetime);
rtc_calendar_result_t rtc_calendar_set_clock_running(uint8_t running);

uint8_t rtc_calendar_is_initialized(void);
uint8_t rtc_calendar_is_device_present(void);
uint8_t rtc_calendar_is_time_valid(void);
rtc_calendar_state_t rtc_calendar_get_state(void);
rtc_calendar_result_t rtc_calendar_get_last_result(void);
void rtc_calendar_get_status(rtc_calendar_status_t *status);


uint8_t rtc_calendar_is_leap_year(uint16_t year);
uint8_t rtc_calendar_days_in_month(uint16_t year, uint8_t month);
uint8_t rtc_calendar_calculate_weekday(uint16_t year, uint8_t month, uint8_t day);
uint8_t rtc_calendar_datetime_is_valid(const rtc_calendar_datetime_t *datetime);

#ifdef __cplusplus
}
#endif

#endif 
