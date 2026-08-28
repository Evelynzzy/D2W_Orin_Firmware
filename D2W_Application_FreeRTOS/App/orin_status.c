#include "gd32f30x.h"

#include "board_pins.h"
#include "orin_status.h"
#include "systick.h"

#define ORIN_STATUS_UINT32_MAX_VALUE    (0xFFFFFFFFUL)
#define ORIN_STATUS_TIME_INVALID    (0xFFFFFFFFUL)

#ifndef ORIN_STATUS_GPIO_SPEED
#define ORIN_STATUS_GPIO_SPEED    GPIO_OSPEED_50MHZ
#endif

#if !defined(POWER_CTRL_CONFIRMED_PIN_MAPPING) || (POWER_CTRL_CONFIRMED_PIN_MAPPING != 1U)
#error "Use the confirmed-mapping board_pins.h"
#endif

#if !defined(ORIN_RESET_STATUS_RCU) || !defined(ORIN_RESET_STATUS_PORT) || !defined(ORIN_RESET_STATUS_PIN) || !defined(ORIN_RESET_STATUS_ACTIVE_LEVEL)
#error "board_pins.h must define the ORIN_RESET_STATUS_* input macros"
#endif

#if (ORIN_RESET_STATUS_ACTIVE_LEVEL != 0U)
#error "The confirmed PA8 SYS_RESET* status input is active low"
#endif

#if (ORIN_STATUS_RESET_ASSERT_DEBOUNCE_SAMPLES == 0U) || (ORIN_STATUS_RESET_ASSERT_DEBOUNCE_SAMPLES > 255U)
#error "ORIN reset-assert debounce must be in the range 1..255"
#endif

#if (ORIN_STATUS_RESET_RELEASE_DEBOUNCE_SAMPLES == 0U) || (ORIN_STATUS_RESET_RELEASE_DEBOUNCE_SAMPLES > 255U)
#error "ORIN reset-release debounce must be in the range 1..255"
#endif

#if (ORIN_STATUS_RUNNING_CONFIRM_MS == 0U)
#error "ORIN_STATUS_RUNNING_CONFIRM_MS must be greater than zero"
#endif

#if (ORIN_STATUS_RESET_STUCK_TIMEOUT_MS == 0U)
#error "ORIN_STATUS_RESET_STUCK_TIMEOUT_MS must be greater than zero"
#endif

#if ((ORIN_STATUS_ENABLE_SOFTWARE_TEST != 0U) && (ORIN_STATUS_ENABLE_SOFTWARE_TEST != 1U))
#error "ORIN_STATUS_ENABLE_SOFTWARE_TEST must be 0 or 1"
#endif

volatile uint8_t orin_reset_pin_state = 0U;
volatile uint8_t orin_reset_active = 0U;
volatile uint8_t orin_running_state = 0U;
volatile uint32_t orin_reset_event_count = 0U;

volatile uint8_t orin_status_test_enable = 0U;
volatile uint8_t orin_status_test_pin_state = 1U;

volatile orin_status_state_t g_orin_status_state = ORIN_STATUS_STATE_UNINITIALIZED;
volatile orin_status_result_t g_orin_status_init_result = ORIN_STATUS_RESULT_NOT_INITIALIZED;
volatile orin_status_result_t g_orin_status_last_result = ORIN_STATUS_RESULT_NOT_INITIALIZED;
volatile power_ctrl_state_t g_orin_status_power_state = POWER_CTRL_STATE_UNINITIALIZED;

volatile uint8_t g_orin_status_initialized = 0U;
volatile uint8_t g_orin_status_raw_reset_active = 0U;
volatile uint8_t g_orin_status_fault_active = 0U;
volatile uint8_t g_orin_status_reset_control_asserted = 0U;

volatile uint32_t g_orin_status_fault_flags = ORIN_STATUS_FAULT_NONE;
volatile uint32_t g_orin_status_latched_fault_flags = ORIN_STATUS_FAULT_NONE;

volatile uint32_t g_orin_status_reset_release_event_count = 0U;
volatile uint32_t g_orin_status_commanded_reset_event_count = 0U;
volatile uint32_t g_orin_status_autonomous_reset_event_count = 0U;
volatile uint32_t g_orin_status_raw_edge_count = 0U;
volatile uint32_t g_orin_status_rejected_glitch_count = 0U;
volatile uint32_t g_orin_status_state_change_count = 0U;
volatile uint32_t g_orin_status_fault_event_count = 0U;
volatile uint32_t g_orin_status_recovery_event_count = 0U;
volatile uint32_t g_orin_status_task_call_count = 0U;

volatile uint32_t g_orin_status_last_update_ms = 0U;
volatile uint32_t g_orin_status_last_state_change_ms = 0U;
volatile uint32_t g_orin_status_last_reset_assert_ms = 0U;
volatile uint32_t g_orin_status_last_reset_release_ms = 0U;
volatile uint32_t g_orin_status_last_fault_ms = 0U;
volatile uint32_t g_orin_status_last_recovery_ms = 0U;
volatile uint32_t g_orin_status_reset_active_duration_ms = 0U;
volatile uint32_t g_orin_status_running_duration_ms = 0U;

static uint8_t s_previous_raw_active = 0U;
static uint8_t s_candidate_active = 0U;
static uint8_t s_candidate_count = 0U;
static power_ctrl_state_t s_previous_power_state = POWER_CTRL_STATE_UNINITIALIZED;

static uint32_t s_run_candidate_since_ms = ORIN_STATUS_TIME_INVALID;
static uint32_t s_reset_active_since_ms = ORIN_STATUS_TIME_INVALID;
static uint32_t s_running_since_ms = ORIN_STATUS_TIME_INVALID;

static void orin_status_counter_increment(volatile uint32_t *counter)
{
	if (*counter != ORIN_STATUS_UINT32_MAX_VALUE)
	{
		(*counter)++;
	}
}

static uint32_t orin_status_enter_critical(void)
{
	uint32_t primask = __get_PRIMASK();

	__disable_irq();
	return primask;
}

static void orin_status_exit_critical(uint32_t primask)
{
	if (primask == 0U)
	{
		__enable_irq();
	}
}

static uint8_t orin_status_configuration_is_valid(void)
{
	if ((ORIN_STATUS_RESET_ASSERT_DEBOUNCE_SAMPLES == 0U) || (ORIN_STATUS_RESET_RELEASE_DEBOUNCE_SAMPLES == 0U) || (ORIN_STATUS_RUNNING_CONFIRM_MS == 0U) || (ORIN_STATUS_RESET_STUCK_TIMEOUT_MS == 0U))
	{
		return 0U;
	}

	return 1U;
}

static uint8_t orin_status_pin_read(void)
{
#if (ORIN_STATUS_ENABLE_SOFTWARE_TEST == 1U)
	if (orin_status_test_enable == ORIN_STATUS_TEST_ENABLE_KEY)
	{
		return (orin_status_test_pin_state != 0U) ? 1U : 0U;
	}
#endif

	return (gpio_input_bit_get(ORIN_RESET_STATUS_PORT, ORIN_RESET_STATUS_PIN) != RESET) ? 1U : 0U;
}

static uint8_t orin_status_active_from_pin(uint8_t pin_level)
{
	return (pin_level == ORIN_RESET_STATUS_ACTIVE_LEVEL) ? 1U : 0U;
}

static void orin_status_state_set(orin_status_state_t new_state, uint32_t current_time)
{
	if (g_orin_status_state == new_state)
	{
		return;
	}

	g_orin_status_state = new_state;
	g_orin_status_last_state_change_ms = current_time;
	orin_status_counter_increment(&g_orin_status_state_change_count);

	if (new_state == ORIN_STATUS_STATE_RUNNING)
	{
		s_running_since_ms = current_time;
	}
	else
	{
		s_running_since_ms = ORIN_STATUS_TIME_INVALID;
		g_orin_status_running_duration_ms = 0U;
	}
}

static void orin_status_filtered_transition_record(uint8_t new_reset_active, power_ctrl_state_t power_state, uint8_t reset_control_asserted, uint32_t current_time)
{
	orin_reset_active = new_reset_active;

	if (new_reset_active != 0U)
	{
		orin_status_counter_increment(&orin_reset_event_count);
		g_orin_status_last_reset_assert_ms = current_time;
		s_run_candidate_since_ms = ORIN_STATUS_TIME_INVALID;

		if (power_state == POWER_CTRL_STATE_ON)
		{
			s_reset_active_since_ms = current_time;

			if (reset_control_asserted != 0U)
			{
				orin_status_counter_increment(&g_orin_status_commanded_reset_event_count);
			}
			else
			{
				orin_status_counter_increment(&g_orin_status_autonomous_reset_event_count);
			}
		}
	}
	else
	{
		orin_status_counter_increment(&g_orin_status_reset_release_event_count);
		g_orin_status_last_reset_release_ms = current_time;
		s_reset_active_since_ms = ORIN_STATUS_TIME_INVALID;
		g_orin_status_reset_active_duration_ms = 0U;

		if (power_state == POWER_CTRL_STATE_ON)
		{
			s_run_candidate_since_ms = current_time;
		}
	}
}

static void orin_status_debounce_update(uint8_t raw_reset_active, power_ctrl_state_t power_state, uint8_t reset_control_asserted, uint32_t current_time)
{
	uint8_t required_count;

	if (raw_reset_active != s_previous_raw_active)
	{
		s_previous_raw_active = raw_reset_active;
		orin_status_counter_increment(&g_orin_status_raw_edge_count);
	}

	if (raw_reset_active != s_candidate_active)
	{
		if ((s_candidate_active != orin_reset_active) && (s_candidate_count != 0U))
		{
			orin_status_counter_increment(&g_orin_status_rejected_glitch_count);
		}

		s_candidate_active = raw_reset_active;
		s_candidate_count = (s_candidate_active != orin_reset_active) ? 1U : 0U;
	}
	else if (s_candidate_active != orin_reset_active)
	{
		if (s_candidate_count != 0xFFU)
		{
			s_candidate_count++;
		}
	}
	else
	{
		s_candidate_count = 0U;
	}

	if (s_candidate_active == orin_reset_active)
	{
		return;
	}

	required_count = (s_candidate_active != 0U) ? ORIN_STATUS_RESET_ASSERT_DEBOUNCE_SAMPLES :
		ORIN_STATUS_RESET_RELEASE_DEBOUNCE_SAMPLES;

	if (s_candidate_count >= required_count)
	{
		s_candidate_count = 0U;
		orin_status_filtered_transition_record(s_candidate_active, power_state, reset_control_asserted, current_time);
	}
}

static void orin_status_power_transition_update(power_ctrl_state_t power_state, uint32_t current_time)
{
	if (power_state == s_previous_power_state)
	{
		return;
	}

	s_previous_power_state = power_state;
	s_run_candidate_since_ms = ORIN_STATUS_TIME_INVALID;
	s_reset_active_since_ms = ORIN_STATUS_TIME_INVALID;

	if (power_state == POWER_CTRL_STATE_ON)
	{
		if (orin_reset_active != 0U)
		{
			s_reset_active_since_ms = current_time;
		}
		else
		{
			s_run_candidate_since_ms = current_time;
		}
	}
}

static void orin_status_durations_update(uint32_t current_time)
{
	if ((g_orin_status_power_state == POWER_CTRL_STATE_ON) && (orin_reset_active != 0U) && (s_reset_active_since_ms != ORIN_STATUS_TIME_INVALID))
	{
		g_orin_status_reset_active_duration_ms = (uint32_t)(current_time - s_reset_active_since_ms);
	}
	else
	{
		g_orin_status_reset_active_duration_ms = 0U;
	}

	if ((g_orin_status_state == ORIN_STATUS_STATE_RUNNING) && (s_running_since_ms != ORIN_STATUS_TIME_INVALID))
	{
		g_orin_status_running_duration_ms = (uint32_t)(current_time - s_running_since_ms);
	}
	else
	{
		g_orin_status_running_duration_ms = 0U;
	}
}

static void orin_status_faults_update(uint32_t current_time)
{
	uint32_t old_flags = g_orin_status_fault_flags;
	uint32_t new_flags = ORIN_STATUS_FAULT_NONE;
	uint32_t newly_asserted;
	uint32_t newly_cleared;

	if (g_orin_status_power_state == POWER_CTRL_STATE_FAULT)
	{
		new_flags |= ORIN_STATUS_FAULT_POWER_CTRL;
	}

	if ((g_orin_status_power_state == POWER_CTRL_STATE_ON) && (orin_reset_active != 0U) && (g_orin_status_reset_active_duration_ms >= ORIN_STATUS_RESET_STUCK_TIMEOUT_MS))
	{
		new_flags |= ORIN_STATUS_FAULT_RESET_STUCK_ACTIVE;
	}

	newly_asserted = new_flags & ~old_flags;
	newly_cleared = old_flags & ~new_flags;

	g_orin_status_fault_flags = new_flags;
	g_orin_status_fault_active = (new_flags != 0U) ? 1U : 0U;

	if (newly_asserted != 0U)
	{
		g_orin_status_latched_fault_flags |= newly_asserted;
		orin_status_counter_increment(&g_orin_status_fault_event_count);
		g_orin_status_last_fault_ms = current_time;
	}

	if (newly_cleared != 0U)
	{
		orin_status_counter_increment(&g_orin_status_recovery_event_count);
		g_orin_status_last_recovery_ms = current_time;
	}
}

static void orin_status_operating_state_update(uint32_t current_time)
{
	orin_status_state_t new_state;

	switch (g_orin_status_power_state)
	{
		case POWER_CTRL_STATE_OFF:
			new_state = ORIN_STATUS_STATE_POWER_OFF;
			break;

		case POWER_CTRL_STATE_POWERING_ON:
			new_state = ORIN_STATUS_STATE_POWERING_ON;
			break;

		case POWER_CTRL_STATE_ON:
			if (g_orin_status_fault_flags != ORIN_STATUS_FAULT_NONE)
			{
				new_state = ORIN_STATUS_STATE_FAULT;
			}
			else if (orin_reset_active != 0U)
			{
				new_state = ORIN_STATUS_STATE_RESET_ACTIVE;
			}
			else
			{
				if (s_run_candidate_since_ms == ORIN_STATUS_TIME_INVALID)
				{
					s_run_candidate_since_ms = current_time;
				}

				if ((uint32_t)(current_time - s_run_candidate_since_ms) >= ORIN_STATUS_RUNNING_CONFIRM_MS)
				{
					new_state = ORIN_STATUS_STATE_RUNNING;
				}
				else
				{
					new_state = ORIN_STATUS_STATE_BOOTING;
				}
			}
			break;

		case POWER_CTRL_STATE_POWERING_OFF:
			new_state = ORIN_STATUS_STATE_POWERING_OFF;
			break;

		case POWER_CTRL_STATE_FAULT:
			new_state = ORIN_STATUS_STATE_FAULT;
			break;

		case POWER_CTRL_STATE_UNINITIALIZED:
		default:
			new_state = ORIN_STATUS_STATE_FAULT;
			break;
	}

	orin_status_state_set(new_state, current_time);
	orin_running_state = (g_orin_status_state == ORIN_STATUS_STATE_RUNNING) ? 1U : 0U;
}

static void orin_status_public_reset(void)
{
	orin_reset_pin_state = 0U;
	orin_reset_active = 0U;
	orin_running_state = 0U;
	orin_reset_event_count = 0U;

	orin_status_test_enable = 0U;
	orin_status_test_pin_state = 1U;

	g_orin_status_state = ORIN_STATUS_STATE_UNINITIALIZED;
	g_orin_status_init_result = ORIN_STATUS_RESULT_NOT_INITIALIZED;
	g_orin_status_last_result = ORIN_STATUS_RESULT_NOT_INITIALIZED;
	g_orin_status_power_state = POWER_CTRL_STATE_UNINITIALIZED;

	g_orin_status_initialized = 0U;
	g_orin_status_raw_reset_active = 0U;
	g_orin_status_fault_active = 0U;
	g_orin_status_reset_control_asserted = 0U;

	g_orin_status_fault_flags = ORIN_STATUS_FAULT_NONE;
	g_orin_status_latched_fault_flags = ORIN_STATUS_FAULT_NONE;

	g_orin_status_reset_release_event_count = 0U;
	g_orin_status_commanded_reset_event_count = 0U;
	g_orin_status_autonomous_reset_event_count = 0U;
	g_orin_status_raw_edge_count = 0U;
	g_orin_status_rejected_glitch_count = 0U;
	g_orin_status_state_change_count = 0U;
	g_orin_status_fault_event_count = 0U;
	g_orin_status_recovery_event_count = 0U;
	g_orin_status_task_call_count = 0U;

	g_orin_status_last_update_ms = 0U;
	g_orin_status_last_state_change_ms = 0U;
	g_orin_status_last_reset_assert_ms = 0U;
	g_orin_status_last_reset_release_ms = 0U;
	g_orin_status_last_fault_ms = 0U;
	g_orin_status_last_recovery_ms = 0U;
	g_orin_status_reset_active_duration_ms = 0U;
	g_orin_status_running_duration_ms = 0U;

	s_previous_raw_active = 0U;
	s_candidate_active = 0U;
	s_candidate_count = 0U;
	s_previous_power_state = POWER_CTRL_STATE_UNINITIALIZED;
	s_run_candidate_since_ms = ORIN_STATUS_TIME_INVALID;
	s_reset_active_since_ms = ORIN_STATUS_TIME_INVALID;
	s_running_since_ms = ORIN_STATUS_TIME_INVALID;
}

orin_status_result_t orin_status_init_checked(void)
{
	uint32_t current_time;
	power_ctrl_status_t power_status;

	orin_status_public_reset();

	if (orin_status_configuration_is_valid() == 0U)
	{
		g_orin_status_init_result = ORIN_STATUS_RESULT_CONFIG_ERROR;
		g_orin_status_last_result = ORIN_STATUS_RESULT_CONFIG_ERROR;
		g_orin_status_state = ORIN_STATUS_STATE_FAULT;
		return ORIN_STATUS_RESULT_CONFIG_ERROR;
	}

	if (power_ctrl_is_initialized() == 0U)
	{
		g_orin_status_init_result = ORIN_STATUS_RESULT_POWER_CTRL_NOT_INITIALIZED;
		g_orin_status_last_result = ORIN_STATUS_RESULT_POWER_CTRL_NOT_INITIALIZED;
		return ORIN_STATUS_RESULT_POWER_CTRL_NOT_INITIALIZED;
	}

	rcu_periph_clock_enable(ORIN_RESET_STATUS_RCU);

	gpio_init(ORIN_RESET_STATUS_PORT, GPIO_MODE_IN_FLOATING, ORIN_STATUS_GPIO_SPEED, ORIN_RESET_STATUS_PIN);

	current_time = systick_get_ms();
	power_ctrl_get_status(&power_status);

	orin_reset_pin_state = orin_status_pin_read();
	g_orin_status_raw_reset_active = orin_status_active_from_pin(orin_reset_pin_state);
	orin_reset_active = g_orin_status_raw_reset_active;

	s_previous_raw_active = g_orin_status_raw_reset_active;
	s_candidate_active = g_orin_status_raw_reset_active;
	s_candidate_count = 0U;

	g_orin_status_power_state = power_status.state;
	g_orin_status_reset_control_asserted = power_status.sys_reset_control_asserted;
	s_previous_power_state = POWER_CTRL_STATE_UNINITIALIZED;

	g_orin_status_initialized = 1U;
	g_orin_status_init_result = ORIN_STATUS_RESULT_OK;
	g_orin_status_last_result = ORIN_STATUS_RESULT_OK;
	g_orin_status_last_update_ms = current_time;

	orin_status_power_transition_update(power_status.state, current_time);
	orin_status_durations_update(current_time);
	orin_status_faults_update(current_time);
	orin_status_operating_state_update(current_time);

	return ORIN_STATUS_RESULT_OK;
}

void orin_status_init(void)
{
	(void)orin_status_init_checked();
}

void orin_status_task_1ms(void)
{
	uint32_t current_time;
	power_ctrl_status_t power_status;

	if (g_orin_status_initialized == 0U)
	{
		g_orin_status_last_result = ORIN_STATUS_RESULT_NOT_INITIALIZED;
		return;
	}

	current_time = systick_get_ms();
	orin_status_counter_increment(&g_orin_status_task_call_count);

	power_ctrl_get_status(&power_status);
	g_orin_status_power_state = power_status.state;
	g_orin_status_reset_control_asserted = power_status.sys_reset_control_asserted;

	orin_status_power_transition_update(power_status.state, current_time);

	orin_reset_pin_state = orin_status_pin_read();
	g_orin_status_raw_reset_active = orin_status_active_from_pin(orin_reset_pin_state);

	orin_status_debounce_update(g_orin_status_raw_reset_active, power_status.state, power_status.sys_reset_control_asserted, current_time);

	orin_status_durations_update(current_time);
	orin_status_faults_update(current_time);
	orin_status_operating_state_update(current_time);
	orin_status_durations_update(current_time);

	g_orin_status_last_update_ms = current_time;
	g_orin_status_last_result = ORIN_STATUS_RESULT_OK;
}

void orin_status_update(void)
{
	orin_status_task_1ms();
}

uint8_t orin_reset_is_active(void)
{
	return orin_reset_active;
}

uint8_t orin_is_running(void)
{
	return orin_running_state;
}

uint32_t orin_reset_event_count_get(void)
{
	return orin_reset_event_count;
}

uint8_t orin_status_is_initialized(void)
{
	return g_orin_status_initialized;
}

uint8_t orin_status_is_healthy(void)
{
	if ((g_orin_status_initialized != 0U) && (g_orin_status_state == ORIN_STATUS_STATE_RUNNING) && (g_orin_status_fault_flags == ORIN_STATUS_FAULT_NONE))
	{
		return 1U;
	}

	return 0U;
}

orin_status_state_t orin_status_get_state(void)
{
	return g_orin_status_state;
}

orin_status_result_t orin_status_get_last_result(void)
{
	return g_orin_status_last_result;
}

uint32_t orin_status_get_fault_flags(void)
{
	return g_orin_status_fault_flags;
}

void orin_status_clear_latched_faults(void)
{
	uint32_t primask = orin_status_enter_critical();

	g_orin_status_latched_fault_flags = g_orin_status_fault_flags;
	orin_status_exit_critical(primask);
}

void orin_status_get_snapshot(orin_status_snapshot_t *snapshot)
{
	uint32_t primask;

	if (snapshot == (orin_status_snapshot_t *)0)
	{
		g_orin_status_last_result = ORIN_STATUS_RESULT_INVALID_ARGUMENT;
		return;
	}

	primask = orin_status_enter_critical();

	snapshot->state = g_orin_status_state;
	snapshot->init_result = g_orin_status_init_result;
	snapshot->last_result = g_orin_status_last_result;
	snapshot->power_state = g_orin_status_power_state;

	snapshot->initialized = g_orin_status_initialized;
	snapshot->raw_pin_level = orin_reset_pin_state;
	snapshot->raw_reset_active = g_orin_status_raw_reset_active;
	snapshot->filtered_reset_active = orin_reset_active;
	snapshot->running = orin_running_state;
	snapshot->fault_active = g_orin_status_fault_active;
	snapshot->reset_control_asserted = g_orin_status_reset_control_asserted;
	snapshot->reserved_u8 = 0U;

	snapshot->fault_flags = g_orin_status_fault_flags;
	snapshot->latched_fault_flags = g_orin_status_latched_fault_flags;

	snapshot->reset_event_count = orin_reset_event_count;
	snapshot->reset_release_event_count = g_orin_status_reset_release_event_count;
	snapshot->commanded_reset_event_count = g_orin_status_commanded_reset_event_count;
	snapshot->autonomous_reset_event_count = g_orin_status_autonomous_reset_event_count;
	snapshot->raw_edge_count = g_orin_status_raw_edge_count;
	snapshot->rejected_glitch_count = g_orin_status_rejected_glitch_count;
	snapshot->state_change_count = g_orin_status_state_change_count;
	snapshot->fault_event_count = g_orin_status_fault_event_count;
	snapshot->recovery_event_count = g_orin_status_recovery_event_count;
	snapshot->task_call_count = g_orin_status_task_call_count;

	snapshot->last_update_ms = g_orin_status_last_update_ms;
	snapshot->last_state_change_ms = g_orin_status_last_state_change_ms;
	snapshot->last_reset_assert_ms = g_orin_status_last_reset_assert_ms;
	snapshot->last_reset_release_ms = g_orin_status_last_reset_release_ms;
	snapshot->last_fault_ms = g_orin_status_last_fault_ms;
	snapshot->last_recovery_ms = g_orin_status_last_recovery_ms;
	snapshot->reset_active_duration_ms = g_orin_status_reset_active_duration_ms;
	snapshot->running_duration_ms = g_orin_status_running_duration_ms;

	orin_status_exit_critical(primask);
}
