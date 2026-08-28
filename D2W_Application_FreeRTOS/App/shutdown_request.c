#include "shutdown_request.h"

#include "board_pins.h"
#include "gd32f30x.h"

#if (!defined(SHUTDOWN_REQ_RCU)) || (!defined(SHUTDOWN_REQ_PORT)) || (!defined(SHUTDOWN_REQ_PIN)) || (!defined(SHUTDOWN_REQ_ACTIVE_LEVEL)) || (!defined(SHUTDOWN_REQ_PORT_SOURCE)) || (!defined(SHUTDOWN_REQ_PIN_SOURCE)) || (!defined(SHUTDOWN_REQ_EXTI_LINE)) || (!defined(SHUTDOWN_REQ_IRQ))
#error "board_pins.h must provide the complete SHUTDOWN_REQ* definition"
#endif

#if (SHUTDOWN_REQ_ACTIVE_LEVEL != 0U)
#error "Orin SHUTDOWN_REQ* must be active low"
#endif

#if (SHUTDOWN_REQUEST_SYS_RESET_TIMEOUT_MS == 0U)
#error "SHUTDOWN_REQUEST_SYS_RESET_TIMEOUT_MS must be greater than zero"
#endif

#if (SHUTDOWN_REQUEST_RESET_STABLE_MS == 0U)
#error "SHUTDOWN_REQUEST_RESET_STABLE_MS must be greater than zero"
#endif

#if (SHUTDOWN_REQUEST_REARM_STABLE_MS == 0U)
#error "SHUTDOWN_REQUEST_REARM_STABLE_MS must be greater than zero"
#endif

#if ((SHUTDOWN_REQUEST_DEFINE_IRQ_HANDLER != 0U) && (SHUTDOWN_REQUEST_DEFINE_IRQ_HANDLER != 1U))
#error "SHUTDOWN_REQUEST_DEFINE_IRQ_HANDLER must be 0 or 1"
#endif

#if ((SHUTDOWN_REQUEST_FAST_PREPARE_IN_ISR != 0U) && (SHUTDOWN_REQUEST_FAST_PREPARE_IN_ISR != 1U))
#error "SHUTDOWN_REQUEST_FAST_PREPARE_IN_ISR must be 0 or 1"
#endif

#define SHUTDOWN_REQUEST_COUNTER_MAX    0xFFFFFFFFUL

volatile uint8_t shutdown_request_pending = 0U;
volatile uint8_t shutdown_request_pin_state = 1U;
volatile uint32_t shutdown_request_event_count = 0U;

volatile shutdown_request_state_t shutdown_request_state = SHUTDOWN_REQUEST_STATE_UNINITIALIZED;
volatile power_ctrl_result_t shutdown_request_last_power_result = POWER_CTRL_RESULT_NOT_INITIALIZED;
volatile uint8_t shutdown_request_completed = 0U;
volatile uint8_t shutdown_request_fault = 0U;
volatile uint8_t shutdown_request_forced_off = 0U;
volatile uint32_t shutdown_request_irq_count = 0U;
volatile uint32_t shutdown_request_polling_recovery_count = 0U;
volatile uint32_t shutdown_request_reset_wait_ms = 0U;

static volatile uint8_t s_initialized = 0U;
static volatile uint8_t s_request_latched = 0U;
static uint32_t s_reset_active_ms = 0U;
static uint32_t s_release_stable_ms = 0U;

static uint32_t shutdown_request_enter_critical(void)
{
	uint32_t primask;

	primask = __get_PRIMASK();
	__disable_irq();
	return primask;
}

static void shutdown_request_exit_critical(uint32_t primask)
{
	if (0U == primask)
	{
		__enable_irq();
	}
}

static void shutdown_request_counter_increment(volatile uint32_t *counter)
{
	if (SHUTDOWN_REQUEST_COUNTER_MAX != *counter)
	{
		(*counter)++;
	}
}

static uint8_t shutdown_request_pin_read(void)
{
	return (RESET != gpio_input_bit_get(SHUTDOWN_REQ_PORT, SHUTDOWN_REQ_PIN)) ? 1U : 0U;
}

static uint8_t shutdown_request_level_is_active(uint8_t pin_level)
{
	return (pin_level == (uint8_t)SHUTDOWN_REQ_ACTIVE_LEVEL) ? 1U : 0U;
}

static void shutdown_request_set_complete(void)
{
	shutdown_request_state = SHUTDOWN_REQUEST_STATE_COMPLETE;
	shutdown_request_completed = 1U;
	shutdown_request_fault = 0U;
	shutdown_request_forced_off = 0U;
}

static void shutdown_request_set_fault(power_ctrl_result_t result)
{
	shutdown_request_last_power_result = result;
	shutdown_request_state = SHUTDOWN_REQUEST_STATE_FAULT;
	shutdown_request_completed = 0U;
	shutdown_request_fault = 1U;
	shutdown_request_forced_off = 0U;
}


static uint8_t shutdown_request_capture_assertion(void)
{
	uint32_t primask;
	uint8_t new_request = 0U;

	primask = shutdown_request_enter_critical();

	if (0U == s_request_latched)
	{
		s_request_latched = 1U;

		if (SHUTDOWN_REQUEST_STATE_ARMED == shutdown_request_state)
		{
			shutdown_request_pending = 1U;
			shutdown_request_counter_increment(&shutdown_request_event_count);

			shutdown_request_state = SHUTDOWN_REQUEST_STATE_REQUEST_LATCHED;
			shutdown_request_last_power_result = POWER_CTRL_RESULT_OK;
			shutdown_request_completed = 0U;
			shutdown_request_fault = 0U;
			shutdown_request_forced_off = 0U;
			shutdown_request_reset_wait_ms = 0U;
			s_reset_active_ms = 0U;
			new_request = 1U;
		}
	}

	shutdown_request_exit_critical(primask);
	return new_request;
}


static void shutdown_request_try_prepare(void)
{
	power_ctrl_state_t power_state;
	power_ctrl_result_t result;

	if (SHUTDOWN_REQUEST_STATE_REQUEST_LATCHED != shutdown_request_state)
	{
		return;
	}

	if (0U == power_ctrl_is_initialized())
	{
		shutdown_request_last_power_result = POWER_CTRL_RESULT_NOT_INITIALIZED;
		return;
	}

	power_state = power_ctrl_get_state();

	if (POWER_CTRL_STATE_POWERING_ON == power_state)
	{
		return;
	}

	if (POWER_CTRL_STATE_POWERING_OFF == power_state)
	{
		shutdown_request_state = SHUTDOWN_REQUEST_STATE_WAIT_SYS_RESET;
		shutdown_request_reset_wait_ms = 0U;
		s_reset_active_ms = 0U;
		return;
	}

	result = orin_shutdown_prepare();
	shutdown_request_last_power_result = result;

	if (POWER_CTRL_RESULT_OK == result)
	{
		power_state = power_ctrl_get_state();

		if (POWER_CTRL_STATE_OFF == power_state)
		{
			shutdown_request_set_complete();
		}
		else if (POWER_CTRL_STATE_POWERING_OFF == power_state)
		{
			shutdown_request_state = SHUTDOWN_REQUEST_STATE_WAIT_SYS_RESET;
			shutdown_request_reset_wait_ms = 0U;
			s_reset_active_ms = 0U;
		}
		else
		{
			shutdown_request_set_fault(POWER_CTRL_RESULT_INVALID_STATE);
		}
	}
	else
	{
		shutdown_request_set_fault(result);
	}
}

void shutdown_request_init(void)
{
	uint8_t new_request = 0U;

	NVIC_DisableIRQ(SHUTDOWN_REQ_IRQ);

	rcu_periph_clock_enable(SHUTDOWN_REQ_RCU);
	rcu_periph_clock_enable(RCU_AF);

	gpio_init(SHUTDOWN_REQ_PORT, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, SHUTDOWN_REQ_PIN);

	gpio_exti_source_select(SHUTDOWN_REQ_PORT_SOURCE, SHUTDOWN_REQ_PIN_SOURCE);
	exti_init(SHUTDOWN_REQ_EXTI_LINE, EXTI_INTERRUPT, EXTI_TRIG_FALLING);
	exti_interrupt_flag_clear(SHUTDOWN_REQ_EXTI_LINE);

	shutdown_request_pending = 0U;
	shutdown_request_event_count = 0U;
	shutdown_request_irq_count = 0U;
	shutdown_request_polling_recovery_count = 0U;
	shutdown_request_reset_wait_ms = 0U;
	shutdown_request_last_power_result = POWER_CTRL_RESULT_OK;
	shutdown_request_completed = 0U;
	shutdown_request_fault = 0U;
	shutdown_request_forced_off = 0U;

	s_request_latched = 0U;
	s_reset_active_ms = 0U;
	s_release_stable_ms = 0U;
	s_initialized = 1U;
	shutdown_request_state = SHUTDOWN_REQUEST_STATE_ARMED;

	shutdown_request_pin_state = shutdown_request_pin_read();
	if (0U != shutdown_request_level_is_active(shutdown_request_pin_state))
	{
		new_request = shutdown_request_capture_assertion();
	}
	else
	{
		s_release_stable_ms = SHUTDOWN_REQUEST_REARM_STABLE_MS;
	}

	NVIC_ClearPendingIRQ(SHUTDOWN_REQ_IRQ);
	NVIC_SetPriority(SHUTDOWN_REQ_IRQ, SHUTDOWN_REQUEST_IRQ_PRIORITY);
	NVIC_EnableIRQ(SHUTDOWN_REQ_IRQ);

	if (0U != new_request)
	{
		shutdown_request_try_prepare();
	}
}

void shutdown_request_task_1ms(void)
{
	uint8_t active;
	uint8_t new_request;
	power_ctrl_state_t power_state;
	power_ctrl_result_t result;

	if (0U == s_initialized)
	{
		return;
	}

	shutdown_request_pin_state = shutdown_request_pin_read();
	active = shutdown_request_level_is_active(shutdown_request_pin_state);

	if (0U != active)
	{
		s_release_stable_ms = 0U;

		new_request = shutdown_request_capture_assertion();
		if (0U != new_request)
		{
			shutdown_request_counter_increment(&shutdown_request_polling_recovery_count);
		}
	}
	else if (s_release_stable_ms < SHUTDOWN_REQUEST_REARM_STABLE_MS)
	{
		s_release_stable_ms++;
	}
	else
	{
		s_release_stable_ms = SHUTDOWN_REQUEST_REARM_STABLE_MS;
	}

	if (SHUTDOWN_REQUEST_STATE_REQUEST_LATCHED == shutdown_request_state)
	{
		shutdown_request_try_prepare();
	}

	if (SHUTDOWN_REQUEST_STATE_WAIT_SYS_RESET != shutdown_request_state)
	{
		return;
	}

	power_state = power_ctrl_get_state();
	if (POWER_CTRL_STATE_OFF == power_state)
	{
		shutdown_request_last_power_result = POWER_CTRL_RESULT_OK;
		shutdown_request_set_complete();
		return;
	}

	if (POWER_CTRL_STATE_FAULT == power_state)
	{
		shutdown_request_set_fault(power_ctrl_get_last_result());
		return;
	}

	if (0U != orin_reset_status_is_active())
	{
		if (s_reset_active_ms < SHUTDOWN_REQUEST_RESET_STABLE_MS)
		{
			s_reset_active_ms++;
		}
	}
	else
	{
		s_reset_active_ms = 0U;
	}

	if (shutdown_request_reset_wait_ms < SHUTDOWN_REQUEST_SYS_RESET_TIMEOUT_MS)
	{
		shutdown_request_reset_wait_ms++;
	}

	if (s_reset_active_ms >= SHUTDOWN_REQUEST_RESET_STABLE_MS)
	{
		result = orin_main_power_off();
		shutdown_request_last_power_result = result;

		if (POWER_CTRL_RESULT_OK == result)
		{
			shutdown_request_set_complete();
		}
		else
		{
			shutdown_request_set_fault(result);
		}
		return;
	}

	if (shutdown_request_reset_wait_ms >= SHUTDOWN_REQUEST_SYS_RESET_TIMEOUT_MS)
	{
		shutdown_request_set_fault(POWER_CTRL_RESULT_SYS_RESET_ASSERT_TIMEOUT);
	}
}

void shutdown_request_irq_handler(void)
{
    uint8_t new_request = 0U;

    if (RESET == exti_interrupt_flag_get(SHUTDOWN_REQ_EXTI_LINE))
    {
			return;
    }

    exti_interrupt_flag_clear(SHUTDOWN_REQ_EXTI_LINE);
    shutdown_request_counter_increment(&shutdown_request_irq_count);

    if (0U == s_initialized)
    {
			return;
    }

    shutdown_request_pin_state = shutdown_request_pin_read();
    if (0U != shutdown_request_level_is_active(shutdown_request_pin_state))
    {
			new_request = shutdown_request_capture_assertion();
    }

#if (SHUTDOWN_REQUEST_FAST_PREPARE_IN_ISR == 1U)
    if (0U != new_request)
    {
			shutdown_request_try_prepare();
    }
#else
    (void)new_request;
#endif
}

#if (SHUTDOWN_REQUEST_DEFINE_IRQ_HANDLER == 1U)
void EXTI10_15_IRQHandler(void)
{
	shutdown_request_irq_handler();
}
#endif

uint8_t shutdown_request_pending_get(void)
{
	return shutdown_request_pending;
}

uint8_t shutdown_request_active_get(void)
{
	if (0U == s_initialized)
	{
		return 0U;
	}

	shutdown_request_pin_state = shutdown_request_pin_read();
	return shutdown_request_level_is_active(shutdown_request_pin_state);
}

void shutdown_request_clear(void)
{
	uint32_t primask;

	primask = shutdown_request_enter_critical();
	shutdown_request_pending = 0U;
	shutdown_request_exit_critical(primask);
}

uint8_t shutdown_request_is_initialized(void)
{
	return s_initialized;
}

uint8_t shutdown_request_is_complete(void)
{
	return shutdown_request_completed;
}

uint8_t shutdown_request_has_fault(void)
{
	return shutdown_request_fault;
}

shutdown_request_state_t shutdown_request_get_state(void)
{
	return shutdown_request_state;
}

power_ctrl_result_t shutdown_request_get_last_power_result(void)
{
	return shutdown_request_last_power_result;
}

void shutdown_request_get_status(shutdown_request_status_t *status)
{
	uint32_t primask;

	if ((shutdown_request_status_t *)0 == status)
	{
		return;
	}

	primask = shutdown_request_enter_critical();

	status->state = shutdown_request_state;
	status->last_power_result = shutdown_request_last_power_result;
	status->initialized = s_initialized;
	status->pin_level = shutdown_request_pin_state;
	status->active = shutdown_request_level_is_active(shutdown_request_pin_state);
	status->pending = shutdown_request_pending;
	status->latched = s_request_latched;
	status->completed = shutdown_request_completed;
	status->fault = shutdown_request_fault;
	status->forced_off = shutdown_request_forced_off;
	status->event_count = shutdown_request_event_count;
	status->irq_count = shutdown_request_irq_count;
	status->polling_recovery_count = shutdown_request_polling_recovery_count;
	status->reset_wait_ms = shutdown_request_reset_wait_ms;

	shutdown_request_exit_critical(primask);
}

shutdown_request_rearm_result_t shutdown_request_rearm(void)
{
	uint32_t primask;
	power_ctrl_state_t power_state;

	if (0U == s_initialized)
	{
		return SHUTDOWN_REQUEST_REARM_NOT_INITIALIZED;
	}

	shutdown_request_pin_state = shutdown_request_pin_read();
	if ((0U != shutdown_request_level_is_active(shutdown_request_pin_state)) || (s_release_stable_ms < SHUTDOWN_REQUEST_REARM_STABLE_MS))
	{
		return SHUTDOWN_REQUEST_REARM_LINE_NOT_RELEASED;
	}

	power_state = power_ctrl_get_state();
	if (POWER_CTRL_STATE_ON != power_state)
	{
		return SHUTDOWN_REQUEST_REARM_POWER_NOT_ON;
	}

	primask = shutdown_request_enter_critical();

	shutdown_request_pending = 0U;
	shutdown_request_state = SHUTDOWN_REQUEST_STATE_ARMED;
	shutdown_request_last_power_result = POWER_CTRL_RESULT_OK;
	shutdown_request_completed = 0U;
	shutdown_request_fault = 0U;
	shutdown_request_forced_off = 0U;
	shutdown_request_reset_wait_ms = 0U;
	s_request_latched = 0U;
	s_reset_active_ms = 0U;

	shutdown_request_exit_critical(primask);
	return SHUTDOWN_REQUEST_REARM_OK;
}

void shutdown_request_force_off(void)
{
	uint32_t primask;

	if (0U == s_initialized)
	{
		return;
	}

	if (0U == power_ctrl_is_initialized())
	{
		shutdown_request_set_fault(POWER_CTRL_RESULT_NOT_INITIALIZED);
		return;
	}

	power_ctrl_force_off();

	primask = shutdown_request_enter_critical();
	shutdown_request_state = SHUTDOWN_REQUEST_STATE_FORCED_OFF;
	shutdown_request_completed = 1U;
	shutdown_request_fault = 1U;
	shutdown_request_forced_off = 1U;
	shutdown_request_exit_critical(primask);
}
