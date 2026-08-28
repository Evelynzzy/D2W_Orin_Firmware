#include "power_ctrl.h"

#include "board_pins.h"
#include "systick.h"
#include "watchdog_ctrl.h"

#if !defined(ORIN_SYS_RESET_CTRL_RCU) || !defined(ORIN_SYS_RESET_CTRL_PORT) || !defined(ORIN_SYS_RESET_CTRL_PIN) || !defined(ORIN_SYS_RESET_CTRL_ACTIVE_LEVEL)
#error "board_pins.h must define ORIN_SYS_RESET_CTRL_RCU/PORT/PIN/ACTIVE_LEVEL"
#endif

#if !defined(ORIN_SYS_RESET_SENSE_RCU) || !defined(ORIN_SYS_RESET_SENSE_PORT) || !defined(ORIN_SYS_RESET_SENSE_PIN) || !defined(ORIN_SYS_RESET_SENSE_ACTIVE_LEVEL)
#error "board_pins.h must define ORIN_SYS_RESET_SENSE_RCU/PORT/PIN/ACTIVE_LEVEL"
#endif

#ifndef SHUTDOWN_REQ_ACTIVE_LEVEL
#define SHUTDOWN_REQ_ACTIVE_LEVEL    0U
#endif

#ifndef USB_VBUS_DET_ACTIVE_LEVEL
#define USB_VBUS_DET_ACTIVE_LEVEL    0U
#endif

#ifndef POWER_CTRL_GPIO_SPEED
#define POWER_CTRL_GPIO_SPEED    GPIO_OSPEED_2MHZ
#endif

#if (ORIN_SYS_RESET_CTRL_ACTIVE_LEVEL != 0U)
#error "The PB15 SYS_RESET* control must be active low"
#endif

#if (ORIN_SYS_RESET_SENSE_ACTIVE_LEVEL != 0U)
#error "The PA8 SYS_RESET* feedback must be active low"
#endif

#if (POWER_CTRL_RESET_PULSE_MS < 5U)
#error "POWER_CTRL_RESET_PULSE_MS must be at least 5 ms"
#endif

#if (POWER_CTRL_VDD_OFF_HOLD_MS < 10U)
#error "POWER_CTRL_VDD_OFF_HOLD_MS must be at least 10 ms"
#endif

#if (POWER_CTRL_RESET_FEEDBACK_TIMEOUT_MS < 1U)
#error "POWER_CTRL_RESET_FEEDBACK_TIMEOUT_MS must be at least 1 ms"
#endif

static volatile uint8_t s_initialized = 0U;
static volatile power_ctrl_state_t s_state = POWER_CTRL_STATE_UNINITIALIZED;
static volatile power_ctrl_result_t s_last_result = POWER_CTRL_RESULT_OK;

volatile uint32_t g_power_diag_wait_ms = 0U;

volatile uint8_t g_power_diag_pb14_latch = 0U;
volatile uint8_t g_power_diag_pb14_pin = 0U;

volatile uint8_t g_power_diag_pa7_latch = 0U;
volatile uint8_t g_power_diag_pa7_pin = 0U;

volatile uint8_t g_power_diag_pb15_latch = 0U;
volatile uint8_t g_power_diag_pb15_pin = 0U;

volatile uint8_t g_power_diag_pa8_pin = 0U;
volatile uint8_t g_power_diag_pa8_ever_high = 0U;

static power_ctrl_result_t power_ctrl_set_result(power_ctrl_result_t result)
{
	s_last_result = result;
	return result;
}

static uint8_t power_ctrl_output_is_high(uint32_t port, uint32_t pin)
{
	return (RESET != gpio_output_bit_get(port, pin)) ? 1U : 0U;
}

static uint8_t power_ctrl_input_is_active(uint32_t port, uint32_t pin, uint32_t active_level)
{
	if (0U == active_level)
	{
		return (RESET == gpio_input_bit_get(port, pin)) ? 1U : 0U;
	}

	return (RESET != gpio_input_bit_get(port, pin)) ? 1U : 0U;
}

static void power_ctrl_diag_reset(void)
{
	g_power_diag_wait_ms = 0U;

	g_power_diag_pb14_latch = 0U;
	g_power_diag_pb14_pin = 0U;

	g_power_diag_pa7_latch = 0U;
	g_power_diag_pa7_pin = 0U;

	g_power_diag_pb15_latch = 0U;
	g_power_diag_pb15_pin = 0U;

	g_power_diag_pa8_pin = 0U;
	g_power_diag_pa8_ever_high = 0U;
}

static void power_ctrl_diag_capture(uint32_t elapsed_ms)
{
	g_power_diag_wait_ms = elapsed_ms;

	g_power_diag_pb14_latch = power_ctrl_output_is_high(ORIN_VDD_PORT, ORIN_VDD_PIN);
	g_power_diag_pb14_pin = (RESET != gpio_input_bit_get(ORIN_VDD_PORT, ORIN_VDD_PIN)) ? 1U : 0U;

	g_power_diag_pa7_latch = power_ctrl_output_is_high(ORIN_POWER_EN_PORT, ORIN_POWER_EN_PIN);
	g_power_diag_pa7_pin = (RESET != gpio_input_bit_get(ORIN_POWER_EN_PORT, ORIN_POWER_EN_PIN)) ? 1U : 0U;

	g_power_diag_pb15_latch = power_ctrl_output_is_high(ORIN_SYS_RESET_CTRL_PORT, ORIN_SYS_RESET_CTRL_PIN);
	g_power_diag_pb15_pin = (RESET != gpio_input_bit_get(ORIN_SYS_RESET_CTRL_PORT, ORIN_SYS_RESET_CTRL_PIN)) ? 1U : 0U;

	g_power_diag_pa8_pin = (RESET != gpio_input_bit_get(ORIN_SYS_RESET_SENSE_PORT, ORIN_SYS_RESET_SENSE_PIN)) ? 1U : 0U;

	if (0U != g_power_diag_pa8_pin)
	{
		g_power_diag_pa8_ever_high = 1U;
	}
}

static uint32_t power_ctrl_required_stable_samples(void)
{
	if (0U == POWER_CTRL_INPUT_STABLE_SAMPLES)
	{
		return 1U;
	}

	return POWER_CTRL_INPUT_STABLE_SAMPLES;
}

static void power_ctrl_enable_gpio_clocks(void)
{
	rcu_periph_clock_enable(ORIN_VDD_RCU);
	rcu_periph_clock_enable(ORIN_POWER_EN_RCU);
	rcu_periph_clock_enable(ORIN_SYS_RESET_CTRL_RCU);
	rcu_periph_clock_enable(ORIN_SYS_RESET_SENSE_RCU);

	rcu_periph_clock_enable(PERIPH_5V_EN_RCU);
	rcu_periph_clock_enable(PERIPH_3V3_EN_RCU);
	rcu_periph_clock_enable(CH634_3V3_EN_RCU);
	rcu_periph_clock_enable(CONNECTOR_3V3_EN_RCU);

	rcu_periph_clock_enable(SHUTDOWN_REQ_RCU);
	rcu_periph_clock_enable(USB_VBUS_DET_RCU);
}

static void power_ctrl_preload_safe_levels(void)
{
	gpio_bit_reset(ORIN_VDD_PORT, ORIN_VDD_PIN);
	gpio_bit_reset(ORIN_POWER_EN_PORT, ORIN_POWER_EN_PIN);

	gpio_bit_set(ORIN_SYS_RESET_CTRL_PORT, ORIN_SYS_RESET_CTRL_PIN);

	gpio_bit_reset(PERIPH_5V_EN_PORT, PERIPH_5V_EN_PIN);
	gpio_bit_reset(PERIPH_3V3_EN_PORT, PERIPH_3V3_EN_PIN);
	gpio_bit_reset(CH634_3V3_EN_PORT, CH634_3V3_EN_PIN);
	gpio_bit_reset(CONNECTOR_3V3_EN_PORT, CONNECTOR_3V3_EN_PIN);
}

static void power_ctrl_outputs_set_safe(void)
{
	gpio_bit_reset(ORIN_POWER_EN_PORT, ORIN_POWER_EN_PIN);

	gpio_bit_reset(CONNECTOR_3V3_EN_PORT, CONNECTOR_3V3_EN_PIN);
	gpio_bit_reset(CH634_3V3_EN_PORT, CH634_3V3_EN_PIN);
	gpio_bit_reset(PERIPH_3V3_EN_PORT, PERIPH_3V3_EN_PIN);
	gpio_bit_reset(PERIPH_5V_EN_PORT, PERIPH_5V_EN_PIN);

	gpio_bit_set(ORIN_SYS_RESET_CTRL_PORT, ORIN_SYS_RESET_CTRL_PIN);
	gpio_bit_reset(ORIN_VDD_PORT, ORIN_VDD_PIN);
}

static power_ctrl_result_t power_ctrl_check_shutdown_during_start(void)
{

	if (POWER_CTRL_STATE_POWERING_ON == s_state)
	{
		return POWER_CTRL_RESULT_OK;
	}

	if (POWER_CTRL_STATE_POWERING_OFF == s_state)
	{
		return POWER_CTRL_RESULT_SHUTDOWN_ACTIVE;
	}

	if (0U != shutdown_request_is_active())
	{
		(void)orin_shutdown_prepare();
		return POWER_CTRL_RESULT_SHUTDOWN_ACTIVE;
	}

	return POWER_CTRL_RESULT_OK;
}

static power_ctrl_result_t power_ctrl_delay_abortable(uint32_t delay_ms)
{
	power_ctrl_result_t result;

	while (delay_ms > 0U)
	{
		result = power_ctrl_check_shutdown_during_start();
		if (POWER_CTRL_RESULT_OK != result)
		{
			return result;
		}

		delay_1ms(1U);
		delay_ms--;
	}

	return power_ctrl_check_shutdown_during_start();
}

static power_ctrl_result_t power_ctrl_wait_for_reset_released(uint32_t timeout_ms)
{
	uint32_t elapsed_ms = 0U;
	uint32_t stable_samples = 0U;
	uint32_t required_samples = power_ctrl_required_stable_samples();
	power_ctrl_result_t result;

	while (elapsed_ms < timeout_ms)
	{
		power_ctrl_diag_capture(elapsed_ms);

		result = power_ctrl_check_shutdown_during_start();
		if (POWER_CTRL_RESULT_OK != result)
		{
			return result;
		}

		if (0U == orin_reset_status_is_active())
		{
			stable_samples++;
			if (stable_samples >= required_samples)
			{
				return POWER_CTRL_RESULT_OK;
			}
		}
		else
		{
			stable_samples = 0U;
		}

		delay_1ms(1U);
		elapsed_ms++;
	}

	power_ctrl_diag_capture(elapsed_ms);

	return POWER_CTRL_RESULT_SYS_RESET_RELEASE_TIMEOUT;
}

static power_ctrl_result_t power_ctrl_wait_for_reset_asserted(uint32_t timeout_ms, uint8_t abort_on_shutdown)
{
	uint32_t elapsed_ms = 0U;
	uint32_t stable_samples = 0U;
	uint32_t required_samples = power_ctrl_required_stable_samples();
	power_ctrl_result_t result;

	while (elapsed_ms < timeout_ms)
	{
		if (0U != abort_on_shutdown)
		{
			result = power_ctrl_check_shutdown_during_start();
			if (POWER_CTRL_RESULT_OK != result)
			{
				return result;
			}
		}

		if (0U != orin_reset_status_is_active())
		{
			stable_samples++;
			if (stable_samples >= required_samples)
			{
				return POWER_CTRL_RESULT_OK;
			}
		}
		else
		{
			stable_samples = 0U;
		}

		delay_1ms(1U);
		elapsed_ms++;
	}

	return POWER_CTRL_RESULT_SYS_RESET_ASSERT_TIMEOUT;
}

static void power_ctrl_abort_start(power_ctrl_state_t final_state)
{
	orin_power_en_disable();
	peripheral_power_off();
	orin_reset_release();

	if (0U != power_ctrl_output_is_high(ORIN_VDD_PORT, ORIN_VDD_PIN))
	{
		delay_1ms(POWER_CTRL_VDD_OFF_HOLD_MS);
		gpio_bit_reset(ORIN_VDD_PORT, ORIN_VDD_PIN);
	}

	s_state = final_state;
}

void power_gpio_init(void)
{
	if (0U != s_initialized)
	{
		return;
	}

	power_ctrl_enable_gpio_clocks();
	power_ctrl_preload_safe_levels();

	gpio_init(ORIN_VDD_PORT, GPIO_MODE_OUT_PP, POWER_CTRL_GPIO_SPEED, ORIN_VDD_PIN);

	gpio_init(ORIN_POWER_EN_PORT, GPIO_MODE_OUT_PP, POWER_CTRL_GPIO_SPEED, ORIN_POWER_EN_PIN);

	gpio_init(ORIN_SYS_RESET_CTRL_PORT, GPIO_MODE_OUT_OD, POWER_CTRL_GPIO_SPEED, ORIN_SYS_RESET_CTRL_PIN);

	gpio_init(PERIPH_5V_EN_PORT, GPIO_MODE_OUT_PP, POWER_CTRL_GPIO_SPEED, PERIPH_5V_EN_PIN);

	gpio_init(PERIPH_3V3_EN_PORT, GPIO_MODE_OUT_PP, POWER_CTRL_GPIO_SPEED, PERIPH_3V3_EN_PIN);

	gpio_init(CH634_3V3_EN_PORT, GPIO_MODE_OUT_PP, POWER_CTRL_GPIO_SPEED, CH634_3V3_EN_PIN);

	gpio_init(CONNECTOR_3V3_EN_PORT, GPIO_MODE_OUT_PP, POWER_CTRL_GPIO_SPEED, CONNECTOR_3V3_EN_PIN);

	gpio_init(ORIN_SYS_RESET_SENSE_PORT, GPIO_MODE_IN_FLOATING, POWER_CTRL_GPIO_SPEED, ORIN_SYS_RESET_SENSE_PIN);

	gpio_init(SHUTDOWN_REQ_PORT, GPIO_MODE_IPU, POWER_CTRL_GPIO_SPEED, SHUTDOWN_REQ_PIN);

	gpio_init(USB_VBUS_DET_PORT, GPIO_MODE_IN_FLOATING, POWER_CTRL_GPIO_SPEED, USB_VBUS_DET_PIN);

	power_ctrl_outputs_set_safe();

	s_initialized = 1U;
	s_state = POWER_CTRL_STATE_OFF;
	s_last_result = POWER_CTRL_RESULT_OK;
}

power_ctrl_result_t peripheral_power_on(void)
{
	power_ctrl_result_t result;

	if (0U == s_initialized)
	{
		return power_ctrl_set_result(POWER_CTRL_RESULT_NOT_INITIALIZED);
	}

	if ((POWER_CTRL_STATE_POWERING_ON != s_state) && (POWER_CTRL_STATE_ON != s_state))
	{
		return power_ctrl_set_result(POWER_CTRL_RESULT_INVALID_STATE);
	}

	if (0U == power_ctrl_output_is_high(ORIN_POWER_EN_PORT, ORIN_POWER_EN_PIN))
	{
		return power_ctrl_set_result(POWER_CTRL_RESULT_INVALID_STATE);
	}

	if ((POWER_CTRL_STATE_ON == s_state) && (0U != orin_reset_status_is_active()))
	{
		return power_ctrl_set_result(POWER_CTRL_RESULT_INVALID_STATE);
	}

	result = power_ctrl_check_shutdown_during_start();
	if (POWER_CTRL_RESULT_OK != result)
	{
		peripheral_power_off();
		return power_ctrl_set_result(result);
	}

	gpio_bit_set(PERIPH_5V_EN_PORT, PERIPH_5V_EN_PIN);
	result = power_ctrl_delay_abortable(POWER_CTRL_PERIPHERAL_SETTLE_MS);
	if (POWER_CTRL_RESULT_OK != result)
	{
		peripheral_power_off();
		return power_ctrl_set_result(result);
	}

	gpio_bit_set(PERIPH_3V3_EN_PORT, PERIPH_3V3_EN_PIN);
	result = power_ctrl_delay_abortable(POWER_CTRL_PERIPHERAL_SETTLE_MS);
	if (POWER_CTRL_RESULT_OK != result)
	{
		peripheral_power_off();
		return power_ctrl_set_result(result);
	}

	gpio_bit_set(CH634_3V3_EN_PORT, CH634_3V3_EN_PIN);
	gpio_bit_set(CONNECTOR_3V3_EN_PORT, CONNECTOR_3V3_EN_PIN);

	result = power_ctrl_check_shutdown_during_start();
	if (POWER_CTRL_RESULT_OK != result)
	{
		peripheral_power_off();
		return power_ctrl_set_result(result);
	}

	return power_ctrl_set_result(POWER_CTRL_RESULT_OK);
}

void peripheral_power_off(void)
{
	if (0U == s_initialized)
	{
		return;
	}

	gpio_bit_reset(CONNECTOR_3V3_EN_PORT, CONNECTOR_3V3_EN_PIN);
	gpio_bit_reset(CH634_3V3_EN_PORT, CH634_3V3_EN_PIN);
	gpio_bit_reset(PERIPH_3V3_EN_PORT, PERIPH_3V3_EN_PIN);
	gpio_bit_reset(PERIPH_5V_EN_PORT, PERIPH_5V_EN_PIN);
}

void orin_reset_assert(void)
{
	if (0U == s_initialized)
	{
		return;
	}

	gpio_bit_reset(ORIN_SYS_RESET_CTRL_PORT, ORIN_SYS_RESET_CTRL_PIN);
}

void orin_reset_release(void)
{
	if (0U == s_initialized)
	{
		return;
	}

	gpio_bit_set(ORIN_SYS_RESET_CTRL_PORT, ORIN_SYS_RESET_CTRL_PIN);
}

power_ctrl_result_t orin_reset_pulse(void)
{
	power_ctrl_result_t result;

	if (0U == s_initialized)
	{
		return power_ctrl_set_result(POWER_CTRL_RESULT_NOT_INITIALIZED);
	}

	if ((POWER_CTRL_STATE_ON != s_state) || (0U == power_ctrl_output_is_high(ORIN_POWER_EN_PORT, ORIN_POWER_EN_PIN)))
	{
		return power_ctrl_set_result(POWER_CTRL_RESULT_INVALID_STATE);
	}

	if (0U != shutdown_request_is_active())
	{
		return power_ctrl_set_result(POWER_CTRL_RESULT_SHUTDOWN_ACTIVE);
	}

	if (0U != orin_reset_status_is_active())
	{
		return power_ctrl_set_result(POWER_CTRL_RESULT_INVALID_STATE);
	}

	orin_reset_assert();

	result = power_ctrl_wait_for_reset_asserted(POWER_CTRL_RESET_FEEDBACK_TIMEOUT_MS, 1U);
	if (POWER_CTRL_RESULT_OK != result)
	{
		orin_reset_release();
		if (POWER_CTRL_RESULT_SHUTDOWN_ACTIVE != result)
		{
			s_state = POWER_CTRL_STATE_FAULT;
		}
		return power_ctrl_set_result(result);
	}

	result = power_ctrl_delay_abortable(POWER_CTRL_RESET_PULSE_MS);
	orin_reset_release();
	if (POWER_CTRL_RESULT_OK != result)
	{
		return power_ctrl_set_result(result);
	}

	result = power_ctrl_wait_for_reset_released(POWER_CTRL_RESET_FEEDBACK_TIMEOUT_MS);

	if ((POWER_CTRL_RESULT_OK != result) && (POWER_CTRL_RESULT_SHUTDOWN_ACTIVE != result))
	{
		s_state = POWER_CTRL_STATE_FAULT;
	}

	return power_ctrl_set_result(result);
}

void orin_power_en_enable(void)
{
	if (0U == s_initialized)
	{
		return;
	}

	gpio_bit_set(ORIN_POWER_EN_PORT, ORIN_POWER_EN_PIN);
}

void orin_power_en_disable(void)
{
	if (0U == s_initialized)
	{
		return;
	}

	gpio_bit_reset(ORIN_POWER_EN_PORT, ORIN_POWER_EN_PIN);
}

power_ctrl_result_t orin_power_on(void)
{
	power_ctrl_result_t result;
	power_ctrl_state_t failure_state;

	if (0U == s_initialized)
	{
		return power_ctrl_set_result(POWER_CTRL_RESULT_NOT_INITIALIZED);
	}

	if (POWER_CTRL_STATE_ON == s_state)
	{
		return power_ctrl_set_result(POWER_CTRL_RESULT_OK);
	}

	if ((POWER_CTRL_STATE_POWERING_ON == s_state) || (POWER_CTRL_STATE_POWERING_OFF == s_state))
	{
		return power_ctrl_set_result(POWER_CTRL_RESULT_BUSY);
	}

	if ((0U != power_ctrl_output_is_high(ORIN_VDD_PORT, ORIN_VDD_PIN)) || (0U != power_ctrl_output_is_high(ORIN_POWER_EN_PORT, ORIN_POWER_EN_PIN)))
	{
		return power_ctrl_set_result(POWER_CTRL_RESULT_INVALID_STATE);
	}

	power_ctrl_diag_reset();
	power_ctrl_outputs_set_safe();
	s_state = POWER_CTRL_STATE_POWERING_ON;

	result = power_ctrl_delay_abortable(POWER_CTRL_STARTUP_GUARD_MS);
	if (POWER_CTRL_RESULT_OK != result)
	{
		power_ctrl_abort_start(POWER_CTRL_STATE_OFF);
		return power_ctrl_set_result(result);
	}

	gpio_bit_set(ORIN_VDD_PORT, ORIN_VDD_PIN);

	result = power_ctrl_delay_abortable(POWER_CTRL_VDD_STABLE_DELAY_MS);
	if (POWER_CTRL_RESULT_OK != result)
	{
		power_ctrl_abort_start(POWER_CTRL_STATE_OFF);
		return power_ctrl_set_result(result);
	}

	orin_reset_release();
	orin_power_en_enable();

	result = peripheral_power_on();
	if (POWER_CTRL_RESULT_OK != result)
	{
		failure_state = (POWER_CTRL_RESULT_SHUTDOWN_ACTIVE == result) ? POWER_CTRL_STATE_OFF : POWER_CTRL_STATE_FAULT;
		power_ctrl_abort_start(failure_state);
		return power_ctrl_set_result(result);
	}

	result = power_ctrl_wait_for_reset_released(POWER_CTRL_SYS_RESET_RELEASE_TIMEOUT_MS);

#if (POWER_CTRL_CARRIER_ONLY_TEST_MODE == 1U)
				if (result == POWER_CTRL_RESULT_SYS_RESET_RELEASE_TIMEOUT)
				{

						result = POWER_CTRL_RESULT_OK;
				}
#endif

	if (POWER_CTRL_RESULT_OK != result)
	{
		failure_state = (POWER_CTRL_RESULT_SHUTDOWN_ACTIVE == result) ? POWER_CTRL_STATE_OFF : POWER_CTRL_STATE_FAULT;
		power_ctrl_abort_start(failure_state);
		return power_ctrl_set_result(result);
	}

	if (POWER_CTRL_STATE_POWERING_ON != s_state)
	{
		power_ctrl_abort_start(POWER_CTRL_STATE_OFF);
		return power_ctrl_set_result(POWER_CTRL_RESULT_SHUTDOWN_ACTIVE);
	}

	s_state = POWER_CTRL_STATE_ON;
	return power_ctrl_set_result(POWER_CTRL_RESULT_OK);
}

power_ctrl_result_t orin_shutdown_prepare(void)
{
	if (0U == s_initialized)
	{
		return power_ctrl_set_result(POWER_CTRL_RESULT_NOT_INITIALIZED);
	}

	if ((POWER_CTRL_STATE_OFF == s_state) && (0U == power_ctrl_output_is_high(ORIN_VDD_PORT, ORIN_VDD_PIN)))
	{
		return power_ctrl_set_result(POWER_CTRL_RESULT_OK);
	}

	s_state = POWER_CTRL_STATE_POWERING_OFF;
	orin_power_en_disable();
	peripheral_power_off();
	orin_reset_release();

	return power_ctrl_set_result(POWER_CTRL_RESULT_OK);
}

power_ctrl_result_t orin_main_power_off(void)
{
	power_ctrl_result_t result;

	if (0U == s_initialized)
	{
		return power_ctrl_set_result(POWER_CTRL_RESULT_NOT_INITIALIZED);
	}

	if (0U == power_ctrl_output_is_high(ORIN_VDD_PORT, ORIN_VDD_PIN))
	{
		s_state = POWER_CTRL_STATE_OFF;
		return power_ctrl_set_result(POWER_CTRL_RESULT_OK);
	}

	if (0U != power_ctrl_output_is_high(ORIN_POWER_EN_PORT, ORIN_POWER_EN_PIN))
	{
		return power_ctrl_set_result(POWER_CTRL_RESULT_INVALID_STATE);
	}

	s_state = POWER_CTRL_STATE_POWERING_OFF;

	result = power_ctrl_wait_for_reset_asserted(POWER_CTRL_SYS_RESET_ASSERT_TIMEOUT_MS, 0U);
	if (POWER_CTRL_RESULT_OK != result)
	{

		s_state = POWER_CTRL_STATE_FAULT;
		return power_ctrl_set_result(result);
	}

	delay_1ms(POWER_CTRL_VDD_OFF_HOLD_MS);
	gpio_bit_reset(ORIN_VDD_PORT, ORIN_VDD_PIN);
	orin_reset_release();

	s_state = POWER_CTRL_STATE_OFF;
	return power_ctrl_set_result(POWER_CTRL_RESULT_OK);
}

power_ctrl_result_t orin_power_off(void)
{
	power_ctrl_result_t result;

	if (0U == s_initialized)
	{
		return power_ctrl_set_result(POWER_CTRL_RESULT_NOT_INITIALIZED);
	}

	result = orin_shutdown_prepare();
	if (POWER_CTRL_RESULT_OK != result)
	{
		return result;
	}

	return orin_main_power_off();
}

void power_ctrl_force_off(void)
{
	if (0U == s_initialized)
	{
		return;
	}

	s_state = POWER_CTRL_STATE_POWERING_OFF;
	orin_power_en_disable();
	peripheral_power_off();
	orin_reset_release();
	gpio_bit_reset(ORIN_VDD_PORT, ORIN_VDD_PIN);

	s_state = POWER_CTRL_STATE_OFF;
	s_last_result = POWER_CTRL_RESULT_OK;
}

uint8_t shutdown_request_is_active(void)
{
	if (0U == s_initialized)
	{
		return 0U;
	}

	return power_ctrl_input_is_active(SHUTDOWN_REQ_PORT, SHUTDOWN_REQ_PIN, SHUTDOWN_REQ_ACTIVE_LEVEL);
}

uint8_t usb_vbus_enable_is_active(void)
{
	if (0U == s_initialized)
	{
		return 0U;
	}

	return power_ctrl_input_is_active(USB_VBUS_DET_PORT, USB_VBUS_DET_PIN, USB_VBUS_DET_ACTIVE_LEVEL);
}

uint8_t orin_reset_status_is_active(void)
{
	if (0U == s_initialized)
	{
		return 1U;
	}

	return power_ctrl_input_is_active(ORIN_SYS_RESET_SENSE_PORT, ORIN_SYS_RESET_SENSE_PIN, ORIN_SYS_RESET_SENSE_ACTIVE_LEVEL);
}

uint8_t power_ctrl_is_initialized(void)
{
	return s_initialized;
}

power_ctrl_state_t power_ctrl_get_state(void)
{
	return s_state;
}

power_ctrl_result_t power_ctrl_get_last_result(void)
{
	return s_last_result;
}

void power_ctrl_get_status(power_ctrl_status_t *status)
{
	if ((power_ctrl_status_t *)0 == status)
	{
		return;
	}

	status->state = s_state;
	status->last_result = s_last_result;
	status->initialized = s_initialized;

	if (0U == s_initialized)
	{
		status->orin_vdd_enabled = 0U;
		status->orin_power_en_enabled = 0U;
		status->sys_reset_control_asserted = 0U;
		status->sys_reset_active = 1U;
		status->shutdown_request_active = 0U;
		status->usb_vbus_enabled = 0U;
		status->peripheral_5v_enabled = 0U;
		status->peripheral_3v3_enabled = 0U;
		status->ch634_3v3_enabled = 0U;
		status->connector_3v3_enabled = 0U;
		return;
	}

	status->orin_vdd_enabled = power_ctrl_output_is_high(ORIN_VDD_PORT, ORIN_VDD_PIN);
	status->orin_power_en_enabled = power_ctrl_output_is_high(ORIN_POWER_EN_PORT, ORIN_POWER_EN_PIN);
	status->sys_reset_control_asserted = (0U == power_ctrl_output_is_high(ORIN_SYS_RESET_CTRL_PORT, ORIN_SYS_RESET_CTRL_PIN)) ? 1U : 0U;
	status->sys_reset_active = orin_reset_status_is_active();
	status->shutdown_request_active = shutdown_request_is_active();
	status->usb_vbus_enabled = usb_vbus_enable_is_active();

	status->peripheral_5v_enabled = power_ctrl_output_is_high(PERIPH_5V_EN_PORT, PERIPH_5V_EN_PIN);
	status->peripheral_3v3_enabled = power_ctrl_output_is_high(PERIPH_3V3_EN_PORT, PERIPH_3V3_EN_PIN);
	status->ch634_3v3_enabled = power_ctrl_output_is_high(CH634_3V3_EN_PORT, CH634_3V3_EN_PIN);
	status->connector_3v3_enabled = power_ctrl_output_is_high(CONNECTOR_3V3_EN_PORT, CONNECTOR_3V3_EN_PIN);
}
