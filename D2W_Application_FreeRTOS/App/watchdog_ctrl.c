#include "watchdog_ctrl.h"

#include "board_pins.h"
#include "gd32f30x.h"
#include "systick.h"

#if (!defined(WATCHDOG_WDI_RCU)) || (!defined(WATCHDOG_WDI_PORT)) || (!defined(WATCHDOG_WDI_PIN)) || (!defined(WATCHDOG_WDO_RCU)) || (!defined(WATCHDOG_WDO_PORT)) || (!defined(WATCHDOG_WDO_PIN))
#error "board_pins.h must define the PB6/WDI and PB7/WDO watchdog pins"
#endif

#ifndef WATCHDOG_WDO_ACTIVE_LEVEL
#define WATCHDOG_WDO_ACTIVE_LEVEL    0U
#endif

#ifndef WATCHDOG_WDO_PORT_SOURCE
#define WATCHDOG_WDO_PORT_SOURCE    GPIO_PORT_SOURCE_GPIOB
#endif

#ifndef WATCHDOG_WDO_PIN_SOURCE
#define WATCHDOG_WDO_PIN_SOURCE    GPIO_PIN_SOURCE_7
#endif

#ifndef WATCHDOG_WDO_EXTI_LINE
#define WATCHDOG_WDO_EXTI_LINE    EXTI_7
#endif

#ifndef WATCHDOG_WDO_IRQ
#define WATCHDOG_WDO_IRQ    EXTI5_9_IRQn
#endif

#if (WATCHDOG_WDO_ACTIVE_LEVEL != 0U)
#error "UM706RS WDO# must be active low"
#endif

#if (WATCHDOG_CTRL_FEED_PERIOD_MS == 0U)
#error "WATCHDOG_CTRL_FEED_PERIOD_MS must be greater than zero"
#endif

#if (WATCHDOG_CTRL_FEED_PERIOD_MS >= 1000U)
#error "WATCHDOG_CTRL_FEED_PERIOD_MS must be less than 1000 ms"
#endif

#if (WATCHDOG_CTRL_FIRST_EDGE_LOW_MS == 0U)
#error "WATCHDOG_CTRL_FIRST_EDGE_LOW_MS must be greater than zero"
#endif

#if (WATCHDOG_CTRL_FIRST_EDGE_HIGH_MS == 0U)
#error "WATCHDOG_CTRL_FIRST_EDGE_HIGH_MS must be greater than zero"
#endif

#if (WATCHDOG_CTRL_WDO_RELEASE_TIMEOUT_MS == 0U)
#error "WATCHDOG_CTRL_WDO_RELEASE_TIMEOUT_MS must be greater than zero"
#endif

#if ((WATCHDOG_CTRL_AUTO_RESET_ON_TIMEOUT != 0U) && (WATCHDOG_CTRL_AUTO_RESET_ON_TIMEOUT != 1U))
#error "WATCHDOG_CTRL_AUTO_RESET_ON_TIMEOUT must be 0 or 1"
#endif

#if ((WATCHDOG_CTRL_USE_WDO_EXTI != 0U) && (WATCHDOG_CTRL_USE_WDO_EXTI != 1U))
#error "WATCHDOG_CTRL_USE_WDO_EXTI must be 0 or 1"
#endif

#if ((WATCHDOG_CTRL_DEFINE_IRQ_HANDLER != 0U) && (WATCHDOG_CTRL_DEFINE_IRQ_HANDLER != 1U))
#error "WATCHDOG_CTRL_DEFINE_IRQ_HANDLER must be 0 or 1"
#endif

#if ((WATCHDOG_CTRL_USE_WDO_EXTI == 0U) && (WATCHDOG_CTRL_DEFINE_IRQ_HANDLER == 1U))
#error "IRQ handler cannot be defined when WDO EXTI is disabled"
#endif

#define WATCHDOG_CTRL_COUNTER_MAX    0xFFFFFFFFUL

#if defined(__CC_ARM)
#define WATCHDOG_CTRL_WEAK    __weak
#elif defined(__GNUC__)
#define WATCHDOG_CTRL_WEAK    __attribute__((weak))
#elif defined(__ICCARM__)
#define WATCHDOG_CTRL_WEAK    __weak
#else
#define WATCHDOG_CTRL_WEAK
#endif

typedef enum
{
	WATCHDOG_TIMEOUT_SOURCE_IRQ = 0,
	WATCHDOG_TIMEOUT_SOURCE_POLLING
} watchdog_timeout_source_t;

volatile uint32_t g_watchdog_feed_count = 0U;
volatile uint8_t g_watchdog_wdi_level = 0U;
volatile uint8_t g_watchdog_timeout = 0U;

volatile uint8_t g_watchdog_wdo_level = 1U;
volatile uint8_t g_watchdog_timeout_latched = 0U;
volatile watchdog_ctrl_state_t g_watchdog_state = WATCHDOG_CTRL_STATE_UNINITIALIZED;
volatile watchdog_ctrl_result_t g_watchdog_last_result = WATCHDOG_CTRL_RESULT_NOT_INITIALIZED;
volatile uint32_t g_watchdog_timeout_count = 0U;
volatile uint32_t g_watchdog_irq_count = 0U;
volatile uint32_t g_watchdog_polling_recovery_count = 0U;
volatile uint32_t g_watchdog_task_call_count = 0U;
volatile uint32_t g_watchdog_elapsed_since_feed_ms = 0U;

volatile uint8_t g_watchdog_test_stop_request = 0U;
volatile uint8_t g_watchdog_test_resume_request = 0U;

static volatile uint8_t s_initialized = 0U;
static volatile uint8_t s_feeding_enabled = 0U;

static uint32_t watchdog_ctrl_enter_critical(void)
{
	uint32_t primask;

	primask = __get_PRIMASK();
	__disable_irq();
	return primask;
}

static void watchdog_ctrl_exit_critical(uint32_t primask)
{
	if (0U == primask)
	{
		__enable_irq();
	}
}

static void watchdog_ctrl_counter_increment(volatile uint32_t *counter)
{
	if (WATCHDOG_CTRL_COUNTER_MAX != *counter)
	{
		(*counter)++;
	}
}

static uint8_t watchdog_ctrl_wdo_read(void)
{
	return (RESET != gpio_input_bit_get(WATCHDOG_WDO_PORT, WATCHDOG_WDO_PIN)) ? 1U : 0U;
}

static uint8_t watchdog_ctrl_wdo_is_active(uint8_t level)
{
	return (level == (uint8_t)WATCHDOG_WDO_ACTIVE_LEVEL) ? 1U : 0U;
}

static void watchdog_ctrl_wdi_write(uint8_t level)
{
	if (0U == level)
	{
		gpio_bit_reset(WATCHDOG_WDI_PORT, WATCHDOG_WDI_PIN);
		g_watchdog_wdi_level = 0U;
	}
	else
	{
		gpio_bit_set(WATCHDOG_WDI_PORT, WATCHDOG_WDI_PIN);
		g_watchdog_wdi_level = 1U;
	}
}

static void watchdog_ctrl_wdi_toggle(void)
{
	if (RESET == gpio_output_bit_get(WATCHDOG_WDI_PORT, WATCHDOG_WDI_PIN))
	{
		watchdog_ctrl_wdi_write(1U);
	}
	else
	{
		watchdog_ctrl_wdi_write(0U);
	}

	watchdog_ctrl_counter_increment(&g_watchdog_feed_count);
	g_watchdog_elapsed_since_feed_ms = 0U;
}

#if (WATCHDOG_CTRL_AUTO_RESET_ON_TIMEOUT == 1U)
static void watchdog_ctrl_reset_mcu(void)
{
	__disable_irq();
	NVIC_SystemReset();

	while (1)
	{
	}
}
#endif


static uint8_t watchdog_ctrl_capture_timeout(watchdog_timeout_source_t source)
{
	uint32_t primask;
	uint8_t new_timeout = 0U;

	primask = watchdog_ctrl_enter_critical();

	g_watchdog_timeout = 1U;
	g_watchdog_wdo_level = 0U;

	if (0U == g_watchdog_timeout_latched)
	{
		g_watchdog_timeout_latched = 1U;
		g_watchdog_state = WATCHDOG_CTRL_STATE_TIMEOUT_DETECTED;
		g_watchdog_last_result = WATCHDOG_CTRL_RESULT_TIMEOUT;
		s_feeding_enabled = 0U;
		watchdog_ctrl_counter_increment(&g_watchdog_timeout_count);

		if (WATCHDOG_TIMEOUT_SOURCE_POLLING == source)
		{
			watchdog_ctrl_counter_increment(&g_watchdog_polling_recovery_count);
		}

		new_timeout = 1U;
	}

	watchdog_ctrl_exit_critical(primask);

#if (WATCHDOG_CTRL_AUTO_RESET_ON_TIMEOUT == 1U)
    if (0U != new_timeout)
    {
			watchdog_ctrl_reset_mcu();
    }
#endif

    return new_timeout;
}

watchdog_ctrl_result_t external_watchdog_init(void)
{
	if (0U != s_initialized)
	{
		return g_watchdog_last_result;
	}

#if (WATCHDOG_CTRL_USE_WDO_EXTI == 1U)
    NVIC_DisableIRQ(WATCHDOG_WDO_IRQ);
#endif

    rcu_periph_clock_enable(WATCHDOG_WDI_RCU);
    rcu_periph_clock_enable(WATCHDOG_WDO_RCU);

#if (WATCHDOG_CTRL_USE_WDO_EXTI == 1U)
    rcu_periph_clock_enable(RCU_AF);
#endif

	gpio_bit_reset(WATCHDOG_WDI_PORT, WATCHDOG_WDI_PIN);
	gpio_init(WATCHDOG_WDI_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, WATCHDOG_WDI_PIN);

	gpio_init(WATCHDOG_WDO_PORT, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, WATCHDOG_WDO_PIN);

	g_watchdog_feed_count = 0U;
	g_watchdog_wdi_level = 0U;
	g_watchdog_timeout = 0U;
	g_watchdog_wdo_level = watchdog_ctrl_wdo_read();
	g_watchdog_timeout_latched = 0U;
	g_watchdog_state = WATCHDOG_CTRL_STATE_UNINITIALIZED;
	g_watchdog_last_result = WATCHDOG_CTRL_RESULT_OK;
	g_watchdog_timeout_count = 0U;
	g_watchdog_irq_count = 0U;
	g_watchdog_polling_recovery_count = 0U;
	g_watchdog_task_call_count = 0U;
	g_watchdog_elapsed_since_feed_ms = 0U;
	g_watchdog_test_stop_request = 0U;
	g_watchdog_test_resume_request = 0U;
	s_feeding_enabled = 0U;

	watchdog_ctrl_wdi_write(0U);
	delay_1ms(WATCHDOG_CTRL_FIRST_EDGE_LOW_MS);
	watchdog_ctrl_wdi_write(1U);
	watchdog_ctrl_counter_increment(&g_watchdog_feed_count);
	delay_1ms(WATCHDOG_CTRL_FIRST_EDGE_HIGH_MS);

#if (WATCHDOG_CTRL_USE_WDO_EXTI == 1U)
    gpio_exti_source_select(WATCHDOG_WDO_PORT_SOURCE, WATCHDOG_WDO_PIN_SOURCE);
    exti_init(WATCHDOG_WDO_EXTI_LINE, EXTI_INTERRUPT, EXTI_TRIG_FALLING);
    exti_interrupt_flag_clear(WATCHDOG_WDO_EXTI_LINE);

    NVIC_ClearPendingIRQ(WATCHDOG_WDO_IRQ);
    NVIC_SetPriority(WATCHDOG_WDO_IRQ, WATCHDOG_CTRL_IRQ_PRIORITY);
#endif

	s_initialized = 1U;
	g_watchdog_wdo_level = watchdog_ctrl_wdo_read();

	if (0U != watchdog_ctrl_wdo_is_active(g_watchdog_wdo_level))
	{
		g_watchdog_timeout = 1U;
		g_watchdog_timeout_latched = 1U;
		g_watchdog_state = WATCHDOG_CTRL_STATE_FAULT;
		g_watchdog_last_result = WATCHDOG_CTRL_RESULT_WDO_STUCK_LOW;
		s_feeding_enabled = 0U;
		return g_watchdog_last_result;
	}

	g_watchdog_timeout = 0U;
	g_watchdog_state = WATCHDOG_CTRL_STATE_RUNNING;
	g_watchdog_last_result = WATCHDOG_CTRL_RESULT_OK;
	s_feeding_enabled = 1U;

#if (WATCHDOG_CTRL_USE_WDO_EXTI == 1U)
    NVIC_EnableIRQ(WATCHDOG_WDO_IRQ);
#endif

	return WATCHDOG_CTRL_RESULT_OK;
}

watchdog_ctrl_result_t external_watchdog_feed_now(void)
{
	if (0U == s_initialized)
	{
		g_watchdog_last_result = WATCHDOG_CTRL_RESULT_NOT_INITIALIZED;
		return g_watchdog_last_result;
	}

	if ((WATCHDOG_CTRL_STATE_RUNNING != g_watchdog_state) || (0U == s_feeding_enabled))
	{
		g_watchdog_last_result = WATCHDOG_CTRL_RESULT_INVALID_STATE;
		return g_watchdog_last_result;
	}

	watchdog_ctrl_wdi_toggle();
	g_watchdog_wdo_level = watchdog_ctrl_wdo_read();

	if (0U != watchdog_ctrl_wdo_is_active(g_watchdog_wdo_level))
	{
		(void)watchdog_ctrl_capture_timeout(WATCHDOG_TIMEOUT_SOURCE_POLLING);
		return g_watchdog_last_result;
	}

	g_watchdog_timeout = 0U;
	g_watchdog_last_result = WATCHDOG_CTRL_RESULT_OK;
	return WATCHDOG_CTRL_RESULT_OK;
}

void external_watchdog_task_1ms(void)
{
	uint8_t new_timeout;

	if (0U == s_initialized)
	{
		return;
	}

	watchdog_ctrl_counter_increment(&g_watchdog_task_call_count);

	if (WATCHDOG_CTRL_TEST_STOP_REQUEST_VALUE == g_watchdog_test_stop_request)
	{
		g_watchdog_test_stop_request = 0U;
		(void)external_watchdog_stop_feeding_for_test();
	}

	if (WATCHDOG_CTRL_TEST_RESUME_REQUEST_VALUE == g_watchdog_test_resume_request)
	{
		g_watchdog_test_resume_request = 0U;
		(void)external_watchdog_resume_feeding();
	}

	g_watchdog_wdo_level = watchdog_ctrl_wdo_read();
	if (0U != watchdog_ctrl_wdo_is_active(g_watchdog_wdo_level))
	{
		new_timeout = watchdog_ctrl_capture_timeout(WATCHDOG_TIMEOUT_SOURCE_POLLING);
		(void)new_timeout;
		return;
	}

	g_watchdog_timeout = 0U;

	if (WATCHDOG_CTRL_COUNTER_MAX != g_watchdog_elapsed_since_feed_ms)
	{
		g_watchdog_elapsed_since_feed_ms++;
	}

	if ((WATCHDOG_CTRL_STATE_RUNNING == g_watchdog_state) && (0U != s_feeding_enabled) && (g_watchdog_elapsed_since_feed_ms >= WATCHDOG_CTRL_FEED_PERIOD_MS))
	{
		(void)external_watchdog_feed_now();
	}
}

void external_watchdog_task(void)
{
	external_watchdog_task_1ms();
}

void external_watchdog_wdo_irq_handler(void)
{
#if (WATCHDOG_CTRL_USE_WDO_EXTI == 1U)
	if (RESET == exti_interrupt_flag_get(WATCHDOG_WDO_EXTI_LINE))
	{
		return;
	}

	exti_interrupt_flag_clear(WATCHDOG_WDO_EXTI_LINE);
	watchdog_ctrl_counter_increment(&g_watchdog_irq_count);

	if (0U == s_initialized)
	{
		return;
	}

	g_watchdog_wdo_level = watchdog_ctrl_wdo_read();
	if (0U != watchdog_ctrl_wdo_is_active(g_watchdog_wdo_level))
	{
		(void)watchdog_ctrl_capture_timeout(WATCHDOG_TIMEOUT_SOURCE_IRQ);
	}
#endif
}

#if ((WATCHDOG_CTRL_USE_WDO_EXTI == 1U) && (WATCHDOG_CTRL_DEFINE_IRQ_HANDLER == 1U))
WATCHDOG_CTRL_WEAK void EXTI5_9_IRQHandler(void)
{
	external_watchdog_wdo_irq_handler();
}
#endif

uint8_t external_watchdog_timeout_is_active(void)
{
	if (0U == s_initialized)
	{
		return 0U;
	}

	g_watchdog_wdo_level = watchdog_ctrl_wdo_read();
	g_watchdog_timeout = watchdog_ctrl_wdo_is_active(g_watchdog_wdo_level);
	return g_watchdog_timeout;
}

uint8_t external_watchdog_is_initialized(void)
{
	return s_initialized;
}

uint8_t external_watchdog_is_running(void)
{
	return ((WATCHDOG_CTRL_STATE_RUNNING == g_watchdog_state) && (0U != s_feeding_enabled)) ? 1U : 0U;
}

watchdog_ctrl_state_t external_watchdog_get_state(void)
{
	return g_watchdog_state;
}

watchdog_ctrl_result_t external_watchdog_get_last_result(void)
{
	return g_watchdog_last_result;
}

void external_watchdog_get_status(watchdog_ctrl_status_t *status)
{
	uint32_t primask;

	if ((watchdog_ctrl_status_t *)0 == status)
	{
		return;
	}

	primask = watchdog_ctrl_enter_critical();

	status->state = g_watchdog_state;
	status->last_result = g_watchdog_last_result;
	status->initialized = s_initialized;
	status->feeding_enabled = s_feeding_enabled;
	status->wdi_level = g_watchdog_wdi_level;
	status->wdo_level = g_watchdog_wdo_level;
	status->timeout_active = g_watchdog_timeout;
	status->timeout_latched = g_watchdog_timeout_latched;
	status->auto_reset_enabled = (uint8_t)WATCHDOG_CTRL_AUTO_RESET_ON_TIMEOUT;
	status->feed_count = g_watchdog_feed_count;
	status->timeout_count = g_watchdog_timeout_count;
	status->irq_count = g_watchdog_irq_count;
	status->polling_recovery_count = g_watchdog_polling_recovery_count;
	status->task_call_count = g_watchdog_task_call_count;
	status->elapsed_since_feed_ms = g_watchdog_elapsed_since_feed_ms;

	watchdog_ctrl_exit_critical(primask);
}

watchdog_ctrl_result_t external_watchdog_stop_feeding_for_test(void)
{
	uint32_t primask;

	if (0U == s_initialized)
	{
		g_watchdog_last_result = WATCHDOG_CTRL_RESULT_NOT_INITIALIZED;
		return g_watchdog_last_result;
	}

	if (WATCHDOG_CTRL_STATE_RUNNING != g_watchdog_state)
	{
		g_watchdog_last_result = WATCHDOG_CTRL_RESULT_INVALID_STATE;
		return g_watchdog_last_result;
	}

	primask = watchdog_ctrl_enter_critical();
	s_feeding_enabled = 0U;
	g_watchdog_state = WATCHDOG_CTRL_STATE_FEED_PAUSED;
	g_watchdog_last_result = WATCHDOG_CTRL_RESULT_OK;
	watchdog_ctrl_exit_critical(primask);

	return WATCHDOG_CTRL_RESULT_OK;
}

watchdog_ctrl_result_t external_watchdog_resume_feeding(void)
{
	uint32_t primask;
	uint32_t wait_ms;

	if (0U == s_initialized)
	{
		g_watchdog_last_result = WATCHDOG_CTRL_RESULT_NOT_INITIALIZED;
		return g_watchdog_last_result;
	}

	if ((WATCHDOG_CTRL_STATE_FEED_PAUSED != g_watchdog_state) && (WATCHDOG_CTRL_STATE_TIMEOUT_DETECTED != g_watchdog_state))
	{
		g_watchdog_last_result = WATCHDOG_CTRL_RESULT_INVALID_STATE;
		return g_watchdog_last_result;
	}

	watchdog_ctrl_wdi_toggle();

	wait_ms = 0U;
	g_watchdog_wdo_level = watchdog_ctrl_wdo_read();
	while ((0U != watchdog_ctrl_wdo_is_active(g_watchdog_wdo_level)) && (wait_ms < WATCHDOG_CTRL_WDO_RELEASE_TIMEOUT_MS))
	{
		delay_1ms(1U);
		wait_ms++;
		g_watchdog_wdo_level = watchdog_ctrl_wdo_read();
	}

	if (0U != watchdog_ctrl_wdo_is_active(g_watchdog_wdo_level))
	{
		g_watchdog_timeout = 1U;
		g_watchdog_state = WATCHDOG_CTRL_STATE_FAULT;
		g_watchdog_last_result = WATCHDOG_CTRL_RESULT_WDO_STUCK_LOW;
		s_feeding_enabled = 0U;
		return g_watchdog_last_result;
	}

	primask = watchdog_ctrl_enter_critical();
	g_watchdog_timeout = 0U;
	g_watchdog_timeout_latched = 0U;
	g_watchdog_state = WATCHDOG_CTRL_STATE_RUNNING;
	g_watchdog_last_result = WATCHDOG_CTRL_RESULT_OK;
	s_feeding_enabled = 1U;
	watchdog_ctrl_exit_critical(primask);

	return WATCHDOG_CTRL_RESULT_OK;
}
