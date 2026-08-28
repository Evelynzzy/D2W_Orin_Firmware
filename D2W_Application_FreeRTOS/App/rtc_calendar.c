#include "rtc_calendar.h"

#include "gd32f30x.h"
#include "systick.h"

#include "board_pins.h"

#if (!defined(RTC_CALENDAR_I2C_PERIPH)) || (!defined(RTC_CALENDAR_I2C_RCU)) || (!defined(RTC_CALENDAR_GPIO_RCU)) || (!defined(RTC_CALENDAR_SCL_PORT)) || (!defined(RTC_CALENDAR_SCL_PIN)) || (!defined(RTC_CALENDAR_SDA_PORT)) || (!defined(RTC_CALENDAR_SDA_PIN))
#error "board_pins.h must define the RTC_CALENDAR_* I2C1 PB10/PB11 macros"
#endif

#if (RTC_CALENDAR_I2C_SPEED_HZ == 0U) || (RTC_CALENDAR_I2C_SPEED_HZ > 400000U)
#error "RTC_CALENDAR_I2C_SPEED_HZ must be 1..400000"
#endif

#if (RTC_CALENDAR_I2C_TIMEOUT_MS == 0U)
#error "RTC_CALENDAR_I2C_TIMEOUT_MS must be greater than zero"
#endif

#if (RTC_CALENDAR_I2C_TIMEOUT_SPINS == 0UL)
#error "RTC_CALENDAR_I2C_TIMEOUT_SPINS must be greater than zero"
#endif

#if (RTC_CALENDAR_UPDATE_PERIOD_MS == 0U)
#error "RTC_CALENDAR_UPDATE_PERIOD_MS must be greater than zero"
#endif

#if ((RTC_CALENDAR_CENTURY_BIT_FOR_2000_2099 != 0U) && (RTC_CALENDAR_CENTURY_BIT_FOR_2000_2099 != 1U))
#error "RTC_CALENDAR_CENTURY_BIT_FOR_2000_2099 must be 0 or 1"
#endif


#define RTC_REG_CONTROL_STATUS_1    (0x00U)
#define RTC_REG_CONTROL_STATUS_2    (0x01U)
#define RTC_REG_SECONDS    (0x02U)
#define RTC_REG_MINUTES    (0x03U)
#define RTC_REG_HOURS    (0x04U)
#define RTC_REG_DAYS    (0x05U)
#define RTC_REG_WEEKDAYS    (0x06U)
#define RTC_REG_CENTURY_MONTHS    (0x07U)
#define RTC_REG_YEARS    (0x08U)

#define RTC_CONTROL1_STOP    (0x20U)
#define RTC_SECONDS_VL    (0x80U)
#define RTC_MONTH_CENTURY    (0x80U)

#define RTC_SECONDS_MASK    (0x7FU)
#define RTC_MINUTES_MASK    (0x7FU)
#define RTC_HOURS_MASK    (0x3FU)
#define RTC_DAYS_MASK    (0x3FU)
#define RTC_WEEKDAYS_MASK    (0x07U)
#define RTC_MONTHS_MASK    (0x1FU)

#define RTC_TIME_REGISTER_COUNT    (7U)
#define RTC_BUS_RECOVERY_CLOCKS    (9U)
#define RTC_BUS_RECOVERY_DELAY_LOOPS    (128U)
#define RTC_YEAR_MIN    (2000U)
#define RTC_YEAR_MAX    (2099U)


volatile rtc_calendar_state_t g_rtc_calendar_state = RTC_CALENDAR_STATE_UNINITIALIZED;
volatile rtc_calendar_result_t g_rtc_calendar_init_result = RTC_CALENDAR_RESULT_NOT_INITIALIZED;
volatile rtc_calendar_result_t g_rtc_calendar_last_result = RTC_CALENDAR_RESULT_NOT_INITIALIZED;
volatile rtc_calendar_datetime_t g_rtc_calendar_datetime = {0U};

volatile uint8_t g_rtc_calendar_initialized = 0U;
volatile uint8_t g_rtc_calendar_device_present = 0U;
volatile uint8_t g_rtc_calendar_time_valid = 0U;
volatile uint8_t g_rtc_calendar_voltage_low = 0U;
volatile uint8_t g_rtc_calendar_clock_running = 0U;

volatile uint32_t g_rtc_calendar_refresh_count = 0U;
volatile uint32_t g_rtc_calendar_set_count = 0U;
volatile uint32_t g_rtc_calendar_error_count = 0U;
volatile uint32_t g_rtc_calendar_consecutive_error_count = 0U;
volatile uint32_t g_rtc_calendar_bus_recovery_count = 0U;
volatile uint32_t g_rtc_calendar_task_call_count = 0U;
volatile uint32_t g_rtc_calendar_last_update_ms = 0U;
volatile uint32_t g_rtc_calendar_last_success_ms = 0U;
volatile uint32_t g_rtc_calendar_last_error_ms = 0U;

static uint32_t s_task_elapsed_ms = 0U;



static void rtc_counter_increment(volatile uint32_t *counter)
{
	if ((counter != (volatile uint32_t *)0) && (*counter != 0xFFFFFFFFUL))
	{
		(*counter)++;
	}
}

static void rtc_recovery_delay(void)
{
	volatile uint32_t count;

	for (count = 0U; count < RTC_BUS_RECOVERY_DELAY_LOOPS; count++)
	{
		__NOP();
	}
}

static uint8_t rtc_result_is_communication_error(rtc_calendar_result_t result)
{
	switch (result)
	{
		case RTC_CALENDAR_RESULT_DEVICE_NOT_FOUND:
		case RTC_CALENDAR_RESULT_BUS_STUCK:
		case RTC_CALENDAR_RESULT_START_TIMEOUT:
		case RTC_CALENDAR_RESULT_ADDRESS_TIMEOUT:
		case RTC_CALENDAR_RESULT_TRANSFER_TIMEOUT:
		case RTC_CALENDAR_RESULT_STOP_TIMEOUT:
		case RTC_CALENDAR_RESULT_BUS_ERROR:
			return 1U;

		default:
			return 0U;
	}
}

static void rtc_datetime_publish(const rtc_calendar_datetime_t *datetime)
{
	if (datetime == (const rtc_calendar_datetime_t *)0)
	{
		return;
	}

	g_rtc_calendar_datetime.year = datetime->year;
	g_rtc_calendar_datetime.month = datetime->month;
	g_rtc_calendar_datetime.day = datetime->day;
	g_rtc_calendar_datetime.weekday = datetime->weekday;
	g_rtc_calendar_datetime.hour = datetime->hour;
	g_rtc_calendar_datetime.minute = datetime->minute;
	g_rtc_calendar_datetime.second = datetime->second;
	g_rtc_calendar_datetime.voltage_low = datetime->voltage_low;
	g_rtc_calendar_datetime.century_flag = datetime->century_flag;
}

static void rtc_public_state_reset(void)
{
	rtc_calendar_datetime_t zero_datetime = {0U};

	g_rtc_calendar_state = RTC_CALENDAR_STATE_UNINITIALIZED;
	g_rtc_calendar_init_result = RTC_CALENDAR_RESULT_NOT_INITIALIZED;
	g_rtc_calendar_last_result = RTC_CALENDAR_RESULT_NOT_INITIALIZED;
	rtc_datetime_publish(&zero_datetime);

	g_rtc_calendar_initialized = 0U;
	g_rtc_calendar_device_present = 0U;
	g_rtc_calendar_time_valid = 0U;
	g_rtc_calendar_voltage_low = 0U;
	g_rtc_calendar_clock_running = 0U;

	g_rtc_calendar_refresh_count = 0U;
	g_rtc_calendar_set_count = 0U;
	g_rtc_calendar_error_count = 0U;
	g_rtc_calendar_consecutive_error_count = 0U;
	g_rtc_calendar_bus_recovery_count = 0U;
	g_rtc_calendar_task_call_count = 0U;
	g_rtc_calendar_last_update_ms = 0U;
	g_rtc_calendar_last_success_ms = 0U;
	g_rtc_calendar_last_error_ms = 0U;
	s_task_elapsed_ms = 0U;
}

static rtc_calendar_result_t rtc_record_result(rtc_calendar_result_t result)
{
	uint32_t current_time = systick_get_ms();

	g_rtc_calendar_last_result = result;

	if (rtc_result_is_communication_error(result) != 0U)
	{
		g_rtc_calendar_state = RTC_CALENDAR_STATE_COMMUNICATION_FAULT;
		g_rtc_calendar_device_present = 0U;
		g_rtc_calendar_time_valid = 0U;
		rtc_counter_increment(&g_rtc_calendar_error_count);
		rtc_counter_increment(&g_rtc_calendar_consecutive_error_count);
		g_rtc_calendar_last_error_ms = current_time;
	}

	return result;
}


uint8_t rtc_calendar_is_leap_year(uint16_t year)
{
	if ((year % 400U) == 0U)
	{
		return 1U;
	}

	if ((year % 100U) == 0U)
	{
		return 0U;
	}

	return ((year % 4U) == 0U) ? 1U : 0U;
}

uint8_t rtc_calendar_days_in_month(uint16_t year, uint8_t month)
{
	static const uint8_t days[12] = {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};

	if ((month == 0U) || (month > 12U))
	{
		return 0U;
	}

	if ((month == 2U) && (rtc_calendar_is_leap_year(year) != 0U))
	{
		return 29U;
	}

	return days[month - 1U];
}

uint8_t rtc_calendar_calculate_weekday(uint16_t year, uint8_t month, uint8_t day)
{
	static const uint8_t month_offset[12] = {0U, 3U, 2U, 5U, 0U, 3U, 5U, 1U, 4U, 6U, 2U, 4U};
	uint32_t adjusted_year;
	uint32_t weekday;

	if ((year < RTC_YEAR_MIN) || (year > RTC_YEAR_MAX) || (month == 0U) || (month > 12U) || (day == 0U) || (day > rtc_calendar_days_in_month(year, month)))
	{
		return 0xFFU;
	}

	adjusted_year = year;
	if (month < 3U)
	{
		adjusted_year--;
	}

	weekday = adjusted_year + (adjusted_year / 4U) - (adjusted_year / 100U) + (adjusted_year / 400U) + month_offset[month - 1U] + day;

	return (uint8_t)(weekday % 7U);
}

uint8_t rtc_calendar_datetime_is_valid(const rtc_calendar_datetime_t *datetime)
{
	uint8_t maximum_day;

	if (datetime == (const rtc_calendar_datetime_t *)0)
	{
		return 0U;
	}

	if ((datetime->year < RTC_YEAR_MIN) || (datetime->year > RTC_YEAR_MAX) || (datetime->month == 0U) || (datetime->month > 12U) || (datetime->weekday > 6U) || (datetime->hour > 23U) || (datetime->minute > 59U) || (datetime->second > 59U))
	{
		return 0U;
	}

	maximum_day = rtc_calendar_days_in_month(datetime->year, datetime->month);
	if ((datetime->day == 0U) || (datetime->day > maximum_day))
	{
		return 0U;
	}

	return 1U;
}

static uint8_t rtc_binary_to_bcd(uint8_t value)
{
	return (uint8_t)(((value / 10U) << 4U) | (value % 10U));
}

static uint8_t rtc_bcd_decode(uint8_t raw, uint8_t mask, uint8_t maximum, uint8_t *decoded)
{
	uint8_t bcd;
	uint8_t low_nibble;
	uint8_t high_nibble;
	uint8_t value;

	if (decoded == (uint8_t *)0)
	{
		return 0U;
	}

	bcd = raw & mask;
	low_nibble = bcd & 0x0FU;
	high_nibble = (bcd >> 4U) & 0x0FU;

	if ((low_nibble > 9U) || (high_nibble > 9U))
	{
		return 0U;
	}

	value = (uint8_t)((high_nibble * 10U) + low_nibble);
	if (value > maximum)
	{
		return 0U;
	}

	*decoded = value;
	return 1U;
}



static void rtc_i2c_gpio_alternate_open_drain(void)
{
	gpio_init(RTC_CALENDAR_SCL_PORT, GPIO_MODE_AF_OD, GPIO_OSPEED_50MHZ, RTC_CALENDAR_SCL_PIN);
	gpio_init(RTC_CALENDAR_SDA_PORT, GPIO_MODE_AF_OD, GPIO_OSPEED_50MHZ, RTC_CALENDAR_SDA_PIN);
}

static void rtc_i2c_peripheral_configure(void)
{
	i2c_deinit(RTC_CALENDAR_I2C_PERIPH);
	i2c_clock_config(RTC_CALENDAR_I2C_PERIPH, RTC_CALENDAR_I2C_SPEED_HZ, I2C_DTCY_2);
	i2c_mode_addr_config(RTC_CALENDAR_I2C_PERIPH, I2C_I2CMODE_ENABLE, I2C_ADDFORMAT_7BITS, 0U);
	i2c_enable(RTC_CALENDAR_I2C_PERIPH);
	i2c_ackpos_config(RTC_CALENDAR_I2C_PERIPH, I2C_ACKPOS_CURRENT);
	i2c_ack_config(RTC_CALENDAR_I2C_PERIPH, I2C_ACK_ENABLE);
}

static void rtc_i2c_clear_error_flags(void)
{
	if (i2c_flag_get(RTC_CALENDAR_I2C_PERIPH, I2C_FLAG_AERR) != RESET)
	{
		i2c_flag_clear(RTC_CALENDAR_I2C_PERIPH, I2C_FLAG_AERR);
	}

	if (i2c_flag_get(RTC_CALENDAR_I2C_PERIPH, I2C_FLAG_BERR) != RESET)
	{
		i2c_flag_clear(RTC_CALENDAR_I2C_PERIPH, I2C_FLAG_BERR);
	}

	if (i2c_flag_get(RTC_CALENDAR_I2C_PERIPH, I2C_FLAG_LOSTARB) != RESET)
	{
		i2c_flag_clear(RTC_CALENDAR_I2C_PERIPH, I2C_FLAG_LOSTARB);
	}

	if (i2c_flag_get(RTC_CALENDAR_I2C_PERIPH, I2C_FLAG_OUERR) != RESET)
	{
		i2c_flag_clear(RTC_CALENDAR_I2C_PERIPH, I2C_FLAG_OUERR);
	}
}

static rtc_calendar_result_t rtc_i2c_current_error(void)
{
	if (i2c_flag_get(RTC_CALENDAR_I2C_PERIPH, I2C_FLAG_AERR) != RESET)
	{
		i2c_flag_clear(RTC_CALENDAR_I2C_PERIPH, I2C_FLAG_AERR);
		return RTC_CALENDAR_RESULT_DEVICE_NOT_FOUND;
	}

	if ((i2c_flag_get(RTC_CALENDAR_I2C_PERIPH, I2C_FLAG_BERR) != RESET) || (i2c_flag_get(RTC_CALENDAR_I2C_PERIPH, I2C_FLAG_LOSTARB) != RESET) || (i2c_flag_get(RTC_CALENDAR_I2C_PERIPH, I2C_FLAG_OUERR) != RESET))
	{
		rtc_i2c_clear_error_flags();
		return RTC_CALENDAR_RESULT_BUS_ERROR;
	}

	return RTC_CALENDAR_RESULT_OK;
}

static rtc_calendar_result_t rtc_i2c_wait_flag(i2c_flag_enum flag, FlagStatus expected, rtc_calendar_result_t timeout_result)
{
	uint32_t start_time = systick_get_ms();
	uint32_t spins = 0U;
	rtc_calendar_result_t error;

	while (i2c_flag_get(RTC_CALENDAR_I2C_PERIPH, flag) != expected)
	{
		error = rtc_i2c_current_error();
		if (error != RTC_CALENDAR_RESULT_OK)
		{
			return error;
		}

		spins++;
		if (((uint32_t)(systick_get_ms() - start_time) >= RTC_CALENDAR_I2C_TIMEOUT_MS) || (spins >= RTC_CALENDAR_I2C_TIMEOUT_SPINS))
		{
			return timeout_result;
		}
	}

	return RTC_CALENDAR_RESULT_OK;
}

static rtc_calendar_result_t rtc_i2c_wait_stop_complete(void)
{
	uint32_t start_time = systick_get_ms();
	uint32_t spins = 0U;

	while ((I2C_CTL0(RTC_CALENDAR_I2C_PERIPH) & I2C_CTL0_STOP) != 0U)
	{
		spins++;
		if (((uint32_t)(systick_get_ms() - start_time) >= RTC_CALENDAR_I2C_TIMEOUT_MS) || (spins >= RTC_CALENDAR_I2C_TIMEOUT_SPINS))
		{
			return RTC_CALENDAR_RESULT_STOP_TIMEOUT;
		}
	}

	return RTC_CALENDAR_RESULT_OK;
}

static void rtc_i2c_receive_defaults_restore(void)
{
	i2c_ackpos_config(RTC_CALENDAR_I2C_PERIPH, I2C_ACKPOS_CURRENT);
	i2c_ack_config(RTC_CALENDAR_I2C_PERIPH, I2C_ACK_ENABLE);
}

static void rtc_i2c_abort(void)
{
	uint32_t spins = 0U;

	i2c_stop_on_bus(RTC_CALENDAR_I2C_PERIPH);
	while (((I2C_CTL0(RTC_CALENDAR_I2C_PERIPH) & I2C_CTL0_STOP) != 0U) && (spins < RTC_CALENDAR_I2C_TIMEOUT_SPINS))
	{
		spins++;
	}

	rtc_i2c_clear_error_flags();
	rtc_i2c_gpio_alternate_open_drain();
	rtc_i2c_peripheral_configure();
}


static rtc_calendar_result_t rtc_i2c_bus_recover(void)
{
	uint8_t clock_index;
	uint8_t recovery_needed;

	i2c_deinit(RTC_CALENDAR_I2C_PERIPH);

	gpio_bit_set(RTC_CALENDAR_SCL_PORT, RTC_CALENDAR_SCL_PIN);
	gpio_bit_set(RTC_CALENDAR_SDA_PORT, RTC_CALENDAR_SDA_PIN);
	gpio_init(RTC_CALENDAR_SCL_PORT, GPIO_MODE_OUT_OD, GPIO_OSPEED_50MHZ, RTC_CALENDAR_SCL_PIN);
	gpio_init(RTC_CALENDAR_SDA_PORT, GPIO_MODE_OUT_OD, GPIO_OSPEED_50MHZ, RTC_CALENDAR_SDA_PIN);
	rtc_recovery_delay();

	recovery_needed = ((gpio_input_bit_get(RTC_CALENDAR_SCL_PORT, RTC_CALENDAR_SCL_PIN) == RESET) || (gpio_input_bit_get(RTC_CALENDAR_SDA_PORT, RTC_CALENDAR_SDA_PIN) == RESET)) ? 1U : 0U;

	if (recovery_needed != 0U)
	{
		rtc_counter_increment(&g_rtc_calendar_bus_recovery_count);

		if (gpio_input_bit_get(RTC_CALENDAR_SCL_PORT, RTC_CALENDAR_SCL_PIN) == RESET)
		{
			rtc_i2c_gpio_alternate_open_drain();
			rtc_i2c_peripheral_configure();
			return RTC_CALENDAR_RESULT_BUS_STUCK;
		}

		for (clock_index = 0U; (clock_index < RTC_BUS_RECOVERY_CLOCKS) && (gpio_input_bit_get(RTC_CALENDAR_SDA_PORT, RTC_CALENDAR_SDA_PIN) == RESET); clock_index++)
		{
			gpio_bit_reset(RTC_CALENDAR_SCL_PORT, RTC_CALENDAR_SCL_PIN);
			rtc_recovery_delay();
			gpio_bit_set(RTC_CALENDAR_SCL_PORT, RTC_CALENDAR_SCL_PIN);
			rtc_recovery_delay();
		}

		gpio_bit_reset(RTC_CALENDAR_SDA_PORT, RTC_CALENDAR_SDA_PIN);
		rtc_recovery_delay();
		gpio_bit_set(RTC_CALENDAR_SCL_PORT, RTC_CALENDAR_SCL_PIN);
		rtc_recovery_delay();
		gpio_bit_set(RTC_CALENDAR_SDA_PORT, RTC_CALENDAR_SDA_PIN);
		rtc_recovery_delay();
	}

	if ((gpio_input_bit_get(RTC_CALENDAR_SCL_PORT, RTC_CALENDAR_SCL_PIN) == RESET) || (gpio_input_bit_get(RTC_CALENDAR_SDA_PORT, RTC_CALENDAR_SDA_PIN) == RESET))
	{
		rtc_i2c_gpio_alternate_open_drain();
		rtc_i2c_peripheral_configure();
		return RTC_CALENDAR_RESULT_BUS_STUCK;
	}

	rtc_i2c_gpio_alternate_open_drain();
	rtc_i2c_peripheral_configure();
	return RTC_CALENDAR_RESULT_OK;
}

static rtc_calendar_result_t rtc_i2c_prepare_transaction(void)
{
	rtc_calendar_result_t result;

	rtc_i2c_receive_defaults_restore();
	rtc_i2c_clear_error_flags();

	result = rtc_i2c_wait_flag(I2C_FLAG_I2CBSY, RESET, RTC_CALENDAR_RESULT_BUS_STUCK);
	if (result == RTC_CALENDAR_RESULT_OK)
	{
		return RTC_CALENDAR_RESULT_OK;
	}

	result = rtc_i2c_bus_recover();
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		return result;
	}

	return rtc_i2c_wait_flag(I2C_FLAG_I2CBSY, RESET, RTC_CALENDAR_RESULT_BUS_STUCK);
}

static rtc_calendar_result_t rtc_i2c_start_and_address(uint32_t direction)
{
	rtc_calendar_result_t result;

	i2c_start_on_bus(RTC_CALENDAR_I2C_PERIPH);
	result = rtc_i2c_wait_flag(I2C_FLAG_SBSEND, SET, RTC_CALENDAR_RESULT_START_TIMEOUT);
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		return result;
	}

	i2c_master_addressing(RTC_CALENDAR_I2C_PERIPH, RTC_CALENDAR_DEVICE_ADDRESS_8BIT, direction);

	return rtc_i2c_wait_flag(I2C_FLAG_ADDSEND, SET, RTC_CALENDAR_RESULT_ADDRESS_TIMEOUT);
}

static rtc_calendar_result_t rtc_i2c_write_registers(uint8_t start_register, const uint8_t *data, uint8_t length)
{
	rtc_calendar_result_t result;
	uint8_t index;

	if ((data == (const uint8_t *)0) || (length == 0U))
	{
		return RTC_CALENDAR_RESULT_INVALID_ARGUMENT;
	}

	result = rtc_i2c_prepare_transaction();
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		return result;
	}

	result = rtc_i2c_start_and_address(I2C_TRANSMITTER);
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		rtc_i2c_abort();
		return result;
	}

	i2c_flag_clear(RTC_CALENDAR_I2C_PERIPH, I2C_FLAG_ADDSEND);

	result = rtc_i2c_wait_flag(I2C_FLAG_TBE, SET, RTC_CALENDAR_RESULT_TRANSFER_TIMEOUT);
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		rtc_i2c_abort();
		return result;
	}

	i2c_data_transmit(RTC_CALENDAR_I2C_PERIPH, start_register);
	result = rtc_i2c_wait_flag(I2C_FLAG_BTC, SET, RTC_CALENDAR_RESULT_TRANSFER_TIMEOUT);
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		rtc_i2c_abort();
		return result;
	}

	for (index = 0U; index < length; index++)
	{
			i2c_data_transmit(RTC_CALENDAR_I2C_PERIPH, data[index]);
			result = rtc_i2c_wait_flag(I2C_FLAG_BTC, SET, RTC_CALENDAR_RESULT_TRANSFER_TIMEOUT);
			if (result != RTC_CALENDAR_RESULT_OK)
			{
				rtc_i2c_abort();
				return result;
			}
	}

	i2c_stop_on_bus(RTC_CALENDAR_I2C_PERIPH);
	result = rtc_i2c_wait_stop_complete();
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		rtc_i2c_abort();
	}

	return result;
}

static rtc_calendar_result_t rtc_i2c_read_registers(uint8_t start_register, uint8_t *data, uint8_t length)
{
	rtc_calendar_result_t result;
	uint8_t remaining;
	uint8_t index = 0U;

	if ((data == (uint8_t *)0) || (length == 0U))
	{
		return RTC_CALENDAR_RESULT_INVALID_ARGUMENT;
	}

	result = rtc_i2c_prepare_transaction();
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		return result;
	}

	result = rtc_i2c_start_and_address(I2C_TRANSMITTER);
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		rtc_i2c_abort();
		return result;
	}

	i2c_flag_clear(RTC_CALENDAR_I2C_PERIPH, I2C_FLAG_ADDSEND);
	result = rtc_i2c_wait_flag(I2C_FLAG_TBE, SET, RTC_CALENDAR_RESULT_TRANSFER_TIMEOUT);
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		rtc_i2c_abort();
		return result;
	}

	i2c_data_transmit(RTC_CALENDAR_I2C_PERIPH, start_register);
	result = rtc_i2c_wait_flag(I2C_FLAG_BTC, SET, RTC_CALENDAR_RESULT_TRANSFER_TIMEOUT);
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		rtc_i2c_abort();
		return result;
	}

	if (length == 2U)
	{
		i2c_ackpos_config(RTC_CALENDAR_I2C_PERIPH, I2C_ACKPOS_NEXT);
	}

	result = rtc_i2c_start_and_address(I2C_RECEIVER);
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		rtc_i2c_abort();
		return result;
	}

	if (length < 3U)
	{
		i2c_ack_config(RTC_CALENDAR_I2C_PERIPH, I2C_ACK_DISABLE);
	}

	i2c_flag_clear(RTC_CALENDAR_I2C_PERIPH, I2C_FLAG_ADDSEND);

	if (length == 1U)
	{
		i2c_stop_on_bus(RTC_CALENDAR_I2C_PERIPH);
		result = rtc_i2c_wait_flag(I2C_FLAG_RBNE, SET, RTC_CALENDAR_RESULT_TRANSFER_TIMEOUT);
		if (result == RTC_CALENDAR_RESULT_OK)
		{
			data[0] = i2c_data_receive(RTC_CALENDAR_I2C_PERIPH);
			result = rtc_i2c_wait_stop_complete();
		}

		rtc_i2c_receive_defaults_restore();
		if (result != RTC_CALENDAR_RESULT_OK)
		{
			rtc_i2c_abort();
		}
		return result;
	}

	if (length == 2U)
	{
		result = rtc_i2c_wait_flag(I2C_FLAG_BTC, SET, RTC_CALENDAR_RESULT_TRANSFER_TIMEOUT);
		if (result == RTC_CALENDAR_RESULT_OK)
		{
			i2c_stop_on_bus(RTC_CALENDAR_I2C_PERIPH);
			result = rtc_i2c_wait_flag(I2C_FLAG_RBNE, SET, RTC_CALENDAR_RESULT_TRANSFER_TIMEOUT);
		}
		if (result == RTC_CALENDAR_RESULT_OK)
		{
			data[0] = i2c_data_receive(RTC_CALENDAR_I2C_PERIPH);
			result = rtc_i2c_wait_flag(I2C_FLAG_RBNE, SET, RTC_CALENDAR_RESULT_TRANSFER_TIMEOUT);
		}
		if (result == RTC_CALENDAR_RESULT_OK)
		{
			data[1] = i2c_data_receive(RTC_CALENDAR_I2C_PERIPH);
			result = rtc_i2c_wait_stop_complete();
		}

		rtc_i2c_receive_defaults_restore();
		if (result != RTC_CALENDAR_RESULT_OK)
		{
			rtc_i2c_abort();
		}
		return result;
	}

	remaining = length;
	while (remaining > 0U)
	{
		if (remaining == 3U)
		{
			result = rtc_i2c_wait_flag(I2C_FLAG_BTC, SET, RTC_CALENDAR_RESULT_TRANSFER_TIMEOUT);
			if (result != RTC_CALENDAR_RESULT_OK)
			{
				rtc_i2c_abort();
				return result;
			}
			i2c_ack_config(RTC_CALENDAR_I2C_PERIPH, I2C_ACK_DISABLE);
		}

		if (remaining == 2U)
		{
			result = rtc_i2c_wait_flag(I2C_FLAG_BTC, SET, RTC_CALENDAR_RESULT_TRANSFER_TIMEOUT);
			if (result != RTC_CALENDAR_RESULT_OK)
			{
				rtc_i2c_abort();
				return result;
			}
			i2c_stop_on_bus(RTC_CALENDAR_I2C_PERIPH);
		}

		result = rtc_i2c_wait_flag(I2C_FLAG_RBNE, SET, RTC_CALENDAR_RESULT_TRANSFER_TIMEOUT);
		if (result != RTC_CALENDAR_RESULT_OK)
		{
			rtc_i2c_abort();
			return result;
		}

		data[index] = i2c_data_receive(RTC_CALENDAR_I2C_PERIPH);
		index++;
		remaining--;
	}

	result = rtc_i2c_wait_stop_complete();
	rtc_i2c_receive_defaults_restore();
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		rtc_i2c_abort();
	}
	return result;
}


static rtc_calendar_result_t rtc_decode_datetime(const uint8_t registers[RTC_TIME_REGISTER_COUNT], rtc_calendar_datetime_t *datetime)
{
	uint8_t year_00_99;

	if ((registers == (const uint8_t *)0) || (datetime == (rtc_calendar_datetime_t *)0))
	{
		return RTC_CALENDAR_RESULT_INVALID_ARGUMENT;
	}

	datetime->voltage_low = ((registers[0] & RTC_SECONDS_VL) != 0U) ? 1U : 0U;
	datetime->century_flag = ((registers[5] & RTC_MONTH_CENTURY) != 0U) ? 1U : 0U;

	if ((rtc_bcd_decode(registers[0], RTC_SECONDS_MASK, 59U, &datetime->second) == 0U) || (rtc_bcd_decode(registers[1], RTC_MINUTES_MASK, 59U, &datetime->minute) == 0U) || (rtc_bcd_decode(registers[2], RTC_HOURS_MASK, 23U, &datetime->hour) == 0U) || (rtc_bcd_decode(registers[3], RTC_DAYS_MASK, 31U, &datetime->day) == 0U) || (rtc_bcd_decode(registers[5], RTC_MONTHS_MASK, 12U, &datetime->month) == 0U) || (rtc_bcd_decode(registers[6], 0xFFU, 99U, &year_00_99) == 0U))
	{
		return RTC_CALENDAR_RESULT_DATA_INVALID;
	}

	datetime->weekday = registers[4] & RTC_WEEKDAYS_MASK;
	datetime->year = (uint16_t)(RTC_YEAR_MIN + year_00_99);

	if (rtc_calendar_datetime_is_valid(datetime) == 0U)
	{
		return RTC_CALENDAR_RESULT_DATA_INVALID;
	}

	return RTC_CALENDAR_RESULT_OK;
}

static void rtc_encode_datetime(const rtc_calendar_datetime_t *datetime, uint8_t registers[RTC_TIME_REGISTER_COUNT])
{
	uint8_t century_bit = (RTC_CALENDAR_CENTURY_BIT_FOR_2000_2099 != 0U) ? RTC_MONTH_CENTURY : 0U;

	registers[0] = rtc_binary_to_bcd(datetime->second);
	registers[1] = rtc_binary_to_bcd(datetime->minute);
	registers[2] = rtc_binary_to_bcd(datetime->hour);
	registers[3] = rtc_binary_to_bcd(datetime->day);
	registers[4] = datetime->weekday & RTC_WEEKDAYS_MASK;
	registers[5] = (uint8_t)(rtc_binary_to_bcd(datetime->month) | century_bit);
	registers[6] = rtc_binary_to_bcd((uint8_t)(datetime->year - RTC_YEAR_MIN));
}

static uint8_t rtc_datetime_equal(const rtc_calendar_datetime_t *left, const rtc_calendar_datetime_t *right)
{
	if ((left == (const rtc_calendar_datetime_t *)0) || (right == (const rtc_calendar_datetime_t *)0))
	{
		return 0U;
	}

	return ((left->year == right->year) && (left->month == right->month) && (left->day == right->day) && (left->weekday == right->weekday) && (left->hour == right->hour) && (left->minute == right->minute) && (left->second == right->second)) ? 1U : 0U;
}



rtc_calendar_result_t rtc_calendar_init(void)
{
	rtc_calendar_result_t result;

	rtc_public_state_reset();

	rcu_periph_clock_enable(RTC_CALENDAR_GPIO_RCU);
	rcu_periph_clock_enable(RTC_CALENDAR_I2C_RCU);

	result = rtc_i2c_bus_recover();
	g_rtc_calendar_initialized = 1U;
	g_rtc_calendar_last_update_ms = systick_get_ms();

	if (result != RTC_CALENDAR_RESULT_OK)
	{
		g_rtc_calendar_init_result = result;
		return rtc_record_result(result);
	}

	result = rtc_calendar_refresh_now();
	g_rtc_calendar_init_result = result;
	return result;
}

rtc_calendar_result_t rtc_calendar_refresh_now(void)
{
	uint8_t control_status_1;
	uint8_t registers[RTC_TIME_REGISTER_COUNT];
	rtc_calendar_datetime_t decoded = {0U};
	rtc_calendar_result_t result;
	uint32_t current_time;

	if (g_rtc_calendar_initialized == 0U)
	{
		g_rtc_calendar_last_result = RTC_CALENDAR_RESULT_NOT_INITIALIZED;
		return RTC_CALENDAR_RESULT_NOT_INITIALIZED;
	}

	result = rtc_i2c_read_registers(RTC_REG_CONTROL_STATUS_1, &control_status_1, 1U);
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		return rtc_record_result(result);
	}

	result = rtc_i2c_read_registers(RTC_REG_SECONDS, registers, RTC_TIME_REGISTER_COUNT);
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		return rtc_record_result(result);
	}

	current_time = systick_get_ms();
	g_rtc_calendar_last_update_ms = current_time;
	g_rtc_calendar_device_present = 1U;
	g_rtc_calendar_consecutive_error_count = 0U;
	g_rtc_calendar_clock_running = ((control_status_1 & RTC_CONTROL1_STOP) == 0U) ? 1U : 0U;
	rtc_counter_increment(&g_rtc_calendar_refresh_count);

	result = rtc_decode_datetime(registers, &decoded);
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		g_rtc_calendar_state = RTC_CALENDAR_STATE_TIME_INVALID;
		g_rtc_calendar_time_valid = 0U;
		g_rtc_calendar_voltage_low = ((registers[0] & RTC_SECONDS_VL) != 0U) ? 1U : 0U;
		rtc_counter_increment(&g_rtc_calendar_error_count);
		g_rtc_calendar_last_error_ms = current_time;
		g_rtc_calendar_last_result = result;
		return result;
	}

	rtc_datetime_publish(&decoded);
	g_rtc_calendar_voltage_low = decoded.voltage_low;

	if (g_rtc_calendar_clock_running == 0U)
	{
		g_rtc_calendar_state = RTC_CALENDAR_STATE_CLOCK_STOPPED;
		g_rtc_calendar_time_valid = 0U;
		g_rtc_calendar_last_result = RTC_CALENDAR_RESULT_CLOCK_STOPPED;
		return RTC_CALENDAR_RESULT_CLOCK_STOPPED;
	}

	if (decoded.voltage_low != 0U)
	{
		g_rtc_calendar_state = RTC_CALENDAR_STATE_TIME_INVALID;
		g_rtc_calendar_time_valid = 0U;
		g_rtc_calendar_last_result = RTC_CALENDAR_RESULT_VOLTAGE_LOW;
		return RTC_CALENDAR_RESULT_VOLTAGE_LOW;
	}

	g_rtc_calendar_state = RTC_CALENDAR_STATE_READY;
	g_rtc_calendar_time_valid = 1U;
	g_rtc_calendar_last_success_ms = current_time;
	g_rtc_calendar_last_result = RTC_CALENDAR_RESULT_OK;
	return RTC_CALENDAR_RESULT_OK;
}

void rtc_calendar_task_1ms(void)
{
	rtc_counter_increment(&g_rtc_calendar_task_call_count);

	if (g_rtc_calendar_initialized == 0U)
	{
		return;
	}

	if (s_task_elapsed_ms < RTC_CALENDAR_UPDATE_PERIOD_MS)
	{
		s_task_elapsed_ms++;
	}

	if (s_task_elapsed_ms >= RTC_CALENDAR_UPDATE_PERIOD_MS)
	{
		s_task_elapsed_ms = 0U;
		(void)rtc_calendar_refresh_now();
	}
}

rtc_calendar_result_t rtc_calendar_get_datetime(rtc_calendar_datetime_t *datetime)
{
	if (datetime == (rtc_calendar_datetime_t *)0)
	{
		return RTC_CALENDAR_RESULT_INVALID_ARGUMENT;
	}

	if (g_rtc_calendar_initialized == 0U)
	{
		return RTC_CALENDAR_RESULT_NOT_INITIALIZED;
	}

	datetime->year = g_rtc_calendar_datetime.year;
	datetime->month = g_rtc_calendar_datetime.month;
	datetime->day = g_rtc_calendar_datetime.day;
	datetime->weekday = g_rtc_calendar_datetime.weekday;
	datetime->hour = g_rtc_calendar_datetime.hour;
	datetime->minute = g_rtc_calendar_datetime.minute;
	datetime->second = g_rtc_calendar_datetime.second;
	datetime->voltage_low = g_rtc_calendar_datetime.voltage_low;
	datetime->century_flag = g_rtc_calendar_datetime.century_flag;

	return (g_rtc_calendar_time_valid != 0U) ? RTC_CALENDAR_RESULT_OK : g_rtc_calendar_last_result;
}

rtc_calendar_result_t rtc_calendar_set_datetime(const rtc_calendar_datetime_t *datetime)
{
	rtc_calendar_datetime_t requested;
	rtc_calendar_datetime_t verified;
	uint8_t original_control;
	uint8_t stopped_control;
	uint8_t running_control;
	uint8_t registers[RTC_TIME_REGISTER_COUNT];
	uint8_t weekday;
	rtc_calendar_result_t result;

	if (g_rtc_calendar_initialized == 0U)
	{
		return RTC_CALENDAR_RESULT_NOT_INITIALIZED;
	}

	if (datetime == (const rtc_calendar_datetime_t *)0)
	{
		return RTC_CALENDAR_RESULT_INVALID_ARGUMENT;
	}

	requested = *datetime;
	weekday = rtc_calendar_calculate_weekday(requested.year, requested.month, requested.day);
	if (weekday > 6U)
	{
		return RTC_CALENDAR_RESULT_INVALID_ARGUMENT;
	}

	requested.weekday = weekday;
	requested.voltage_low = 0U;
	requested.century_flag = RTC_CALENDAR_CENTURY_BIT_FOR_2000_2099;

	if (rtc_calendar_datetime_is_valid(&requested) == 0U)
	{
		return RTC_CALENDAR_RESULT_INVALID_ARGUMENT;
	}

	rtc_encode_datetime(&requested, registers);

	result = rtc_i2c_read_registers(RTC_REG_CONTROL_STATUS_1, &original_control,
																	1U);
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		return rtc_record_result(result);
	}

	stopped_control = original_control | RTC_CONTROL1_STOP;
	result = rtc_i2c_write_registers(RTC_REG_CONTROL_STATUS_1, &stopped_control, 1U);
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		return rtc_record_result(result);
	}

	result = rtc_i2c_write_registers(RTC_REG_SECONDS, registers, RTC_TIME_REGISTER_COUNT);
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		(void)rtc_i2c_write_registers(RTC_REG_CONTROL_STATUS_1, &original_control, 1U);
		return rtc_record_result(result);
	}

	running_control = original_control & (uint8_t)~RTC_CONTROL1_STOP;
	result = rtc_i2c_write_registers(RTC_REG_CONTROL_STATUS_1, &running_control, 1U);
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		return rtc_record_result(result);
	}

	result = rtc_calendar_refresh_now();
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		return result;
	}

	(void)rtc_calendar_get_datetime(&verified);
	if (rtc_datetime_equal(&requested, &verified) == 0U)
	{
		g_rtc_calendar_state = RTC_CALENDAR_STATE_TIME_INVALID;
		g_rtc_calendar_time_valid = 0U;
		g_rtc_calendar_last_result = RTC_CALENDAR_RESULT_VERIFY_FAILED;
		rtc_counter_increment(&g_rtc_calendar_error_count);
		g_rtc_calendar_last_error_ms = systick_get_ms();
		return RTC_CALENDAR_RESULT_VERIFY_FAILED;
	}

	rtc_counter_increment(&g_rtc_calendar_set_count);
	g_rtc_calendar_last_result = RTC_CALENDAR_RESULT_OK;
	return RTC_CALENDAR_RESULT_OK;
}

rtc_calendar_result_t rtc_calendar_set_clock_running(uint8_t running)
{
	uint8_t control_status_1;
	uint8_t verify_control;
	rtc_calendar_result_t result;

	if (g_rtc_calendar_initialized == 0U)
	{
		return RTC_CALENDAR_RESULT_NOT_INITIALIZED;
	}

	if (running > 1U)
	{
		return RTC_CALENDAR_RESULT_INVALID_ARGUMENT;
	}

	result = rtc_i2c_read_registers(RTC_REG_CONTROL_STATUS_1, &control_status_1, 1U);
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		return rtc_record_result(result);
	}

	if (running != 0U)
	{
		control_status_1 &= (uint8_t)~RTC_CONTROL1_STOP;
	}
	else
	{
		control_status_1 |= RTC_CONTROL1_STOP;
	}

	result = rtc_i2c_write_registers(RTC_REG_CONTROL_STATUS_1, &control_status_1, 1U);
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		return rtc_record_result(result);
	}

	result = rtc_i2c_read_registers(RTC_REG_CONTROL_STATUS_1, &verify_control, 1U);
	if (result != RTC_CALENDAR_RESULT_OK)
	{
		return rtc_record_result(result);
	}

	if ((((verify_control & RTC_CONTROL1_STOP) == 0U) ? 1U : 0U) != running)
	{
		g_rtc_calendar_last_result = RTC_CALENDAR_RESULT_VERIFY_FAILED;
		return RTC_CALENDAR_RESULT_VERIFY_FAILED;
	}

	g_rtc_calendar_clock_running = running;

	if (running != 0U)
	{
		return rtc_calendar_refresh_now();
	}

	g_rtc_calendar_state = RTC_CALENDAR_STATE_CLOCK_STOPPED;
	g_rtc_calendar_time_valid = 0U;
	g_rtc_calendar_last_result = RTC_CALENDAR_RESULT_OK;
	return RTC_CALENDAR_RESULT_OK;
}

uint8_t rtc_calendar_is_initialized(void)
{
	return g_rtc_calendar_initialized;
}

uint8_t rtc_calendar_is_device_present(void)
{
	return g_rtc_calendar_device_present;
}

uint8_t rtc_calendar_is_time_valid(void)
{
    return g_rtc_calendar_time_valid;
}

rtc_calendar_state_t rtc_calendar_get_state(void)
{
	return g_rtc_calendar_state;
}

rtc_calendar_result_t rtc_calendar_get_last_result(void)
{
	return g_rtc_calendar_last_result;
}

void rtc_calendar_get_status(rtc_calendar_status_t *status)
{
	if (status == (rtc_calendar_status_t *)0)
	{
		return;
	}

	status->state = g_rtc_calendar_state;
	status->init_result = g_rtc_calendar_init_result;
	status->last_result = g_rtc_calendar_last_result;

	status->datetime.year = g_rtc_calendar_datetime.year;
	status->datetime.month = g_rtc_calendar_datetime.month;
	status->datetime.day = g_rtc_calendar_datetime.day;
	status->datetime.weekday = g_rtc_calendar_datetime.weekday;
	status->datetime.hour = g_rtc_calendar_datetime.hour;
	status->datetime.minute = g_rtc_calendar_datetime.minute;
	status->datetime.second = g_rtc_calendar_datetime.second;
	status->datetime.voltage_low = g_rtc_calendar_datetime.voltage_low;
	status->datetime.century_flag = g_rtc_calendar_datetime.century_flag;

	status->initialized = g_rtc_calendar_initialized;
	status->device_present = g_rtc_calendar_device_present;
	status->time_valid = g_rtc_calendar_time_valid;
	status->voltage_low = g_rtc_calendar_voltage_low;
	status->clock_running = g_rtc_calendar_clock_running;

	status->refresh_count = g_rtc_calendar_refresh_count;
	status->set_count = g_rtc_calendar_set_count;
	status->error_count = g_rtc_calendar_error_count;
	status->consecutive_error_count = g_rtc_calendar_consecutive_error_count;
	status->bus_recovery_count = g_rtc_calendar_bus_recovery_count;
	status->task_call_count = g_rtc_calendar_task_call_count;
	status->last_update_ms = g_rtc_calendar_last_update_ms;
	status->last_success_ms = g_rtc_calendar_last_success_ms;
	status->last_error_ms = g_rtc_calendar_last_error_ms;
}
