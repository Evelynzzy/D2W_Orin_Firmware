#include "freertos_app.h"

#include <stdint.h>

#include "gd32f30x.h"
#include "power_ctrl.h"

#define FREERTOS_APP_COUNTER_MAX    (0xFFFFFFFFUL)

volatile uint8_t g_freertos_scheduler_running = 0U;
volatile uint32_t g_freertos_heartbeat_count = 0U;
volatile freertos_app_fault_t g_freertos_fault_code = FREERTOS_APP_FAULT_NONE;
volatile uint32_t g_freertos_fault_line = 0U;
volatile uint32_t g_freertos_fault_task_handle = 0U;

static StaticTask_t s_idle_tcb;
static StackType_t s_idle_stack[FREERTOS_APP_IDLE_STACK_WORDS];

static StaticTask_t s_timer_tcb;
static StackType_t s_timer_stack[FREERTOS_APP_TIMER_STACK_WORDS];

void freertos_app_heartbeat(void)
{
	g_freertos_scheduler_running = 1U;

	if (g_freertos_heartbeat_count != FREERTOS_APP_COUNTER_MAX)
	{
			g_freertos_heartbeat_count++;
	}
}

void freertos_app_fatal(freertos_app_fault_t fault)
{
	g_freertos_fault_code = fault;
	__disable_irq();

	if (power_ctrl_is_initialized() != 0U)
	{
		power_ctrl_force_off();
	}

	__DSB();

	while (1)
	{
		__NOP();
	}
}

void freertos_assert_failed(const char *file, uint32_t line)
{
	(void)file;
	g_freertos_fault_line = line;
	freertos_app_fatal(FREERTOS_APP_FAULT_ASSERT);
}

void vApplicationGetIdleTaskMemory(StaticTask_t **idle_tcb, StackType_t **idle_stack, configSTACK_DEPTH_TYPE *idle_stack_size)
{
	*idle_tcb = &s_idle_tcb;
	*idle_stack = s_idle_stack;
	*idle_stack_size = (configSTACK_DEPTH_TYPE)FREERTOS_APP_IDLE_STACK_WORDS;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **timer_tcb, StackType_t **timer_stack, configSTACK_DEPTH_TYPE *timer_stack_size)
{
	*timer_tcb = &s_timer_tcb;
	*timer_stack = s_timer_stack;
	*timer_stack_size = (configSTACK_DEPTH_TYPE)FREERTOS_APP_TIMER_STACK_WORDS;
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
	(void)task_name;
	g_freertos_fault_task_handle = (uint32_t)(uintptr_t)task;
	freertos_app_fatal(FREERTOS_APP_FAULT_STACK_OVERFLOW);
}
