#include "system_tasks.h"

#include "event_groups.h"
#include "queue.h"
#include "timers.h"

#include "board_pins.h"
#include "freertos_app.h"
#include "gd32f30x_rcu.h"
#include "orin_status.h"
#include "power_fail.h"
#include "systick.h"
#include "voltage_monitor.h"

#if ((POWER_FAIL_IRQ_PRIORITY < configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY) || (POWER_FAIL_IRQ_PRIORITY > configLIBRARY_LOWEST_INTERRUPT_PRIORITY))
#error "POWER_FAIL IRQ priority cannot call FreeRTOS FromISR APIs"
#endif

#if ((WATCHDOG_CTRL_IRQ_PRIORITY < configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY) || (WATCHDOG_CTRL_IRQ_PRIORITY > configLIBRARY_LOWEST_INTERRUPT_PRIORITY))
#error "WATCHDOG IRQ priority cannot call FreeRTOS FromISR APIs"
#endif

#if ((SHUTDOWN_REQUEST_IRQ_PRIORITY < configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY) || (SHUTDOWN_REQUEST_IRQ_PRIORITY > configLIBRARY_LOWEST_INTERRUPT_PRIORITY))
#error "SHUTDOWN IRQ priority cannot call FreeRTOS FromISR APIs"
#endif

#define SYSTEM_COUNTER_MAX    (0xFFFFFFFFUL)
#define SYSTEM_FORCE_OFF_REQUEST_VALUE    (1U)
#define SYSTEM_ORIN_RESET_OUTPUT_TEST_KEY    (0xA5U)
#define SYSTEM_RTC_SET_TEST_KEY    (0xA5U)

#define SYSTEM_BOOT_STAGE_WATCHDOG_FAULT    (0xE1U)
#define SYSTEM_BOOT_STAGE_POWER_ON_FAULT    (0xE2U)

#define SYSTEM_WATCHDOG_TASK_PERIOD_MS    (1U)
#define SYSTEM_POWER_MANAGER_PERIOD_MS    (1U)
#define SYSTEM_STORAGE_TASK_PERIOD_MS    (10U)
#define SYSTEM_ORIN_STATUS_PERIOD_MS    (1U)
#define SYSTEM_VOLTAGE_PERIOD_MS    (200U)
#define SYSTEM_RTC_PERIOD_MS    (1000U)
#define SYSTEM_DIAGNOSTICS_TIMER_PERIOD_MS    (1000U)

#define SYSTEM_NOTIFY_WATCHDOG_FEED_NOW    (1UL << 0)
#define SYSTEM_NOTIFY_WATCHDOG_IRQ    (1UL << 1)
#define SYSTEM_NOTIFY_POWER_MANAGER_IRQ    (1UL << 0)
#define SYSTEM_NOTIFY_STORAGE_IRQ    (1UL << 0)

#define SYSTEM_WATCHDOG_EVENT_MASK    (SYSTEM_EVENT_WATCHDOG_FAULT)
#define SYSTEM_POWER_EVENT_MASK    (SYSTEM_EVENT_POWER_ON | SYSTEM_EVENT_SHUTDOWN_READY | SYSTEM_EVENT_SHUTDOWN_ACTIVE | SYSTEM_EVENT_SHUTDOWN_FAULT)
#define SYSTEM_STORAGE_EVENT_MASK    (SYSTEM_EVENT_POWER_FAIL_READY | SYSTEM_EVENT_POWER_FAIL_LATCHED)
#define SYSTEM_ORIN_EVENT_MASK    (SYSTEM_EVENT_ORIN_FAULT)
#define SYSTEM_VOLTAGE_EVENT_MASK    (SYSTEM_EVENT_VOLTAGE_READY | SYSTEM_EVENT_VOLTAGE_FAULT)
#define SYSTEM_RTC_EVENT_MASK    (SYSTEM_EVENT_RTC_READY)


volatile uint8_t g_boot_stage = 0U;
volatile power_ctrl_result_t g_power_on_result = POWER_CTRL_RESULT_NOT_INITIALIZED;
volatile power_ctrl_state_t g_power_state = POWER_CTRL_STATE_UNINITIALIZED;

volatile watchdog_ctrl_result_t g_watchdog_init_result = WATCHDOG_CTRL_RESULT_NOT_INITIALIZED;
volatile watchdog_ctrl_result_t g_watchdog_manual_feed_result = WATCHDOG_CTRL_RESULT_NOT_INITIALIZED;

volatile uint8_t g_shutdown_module_initialized = 0U;
volatile shutdown_request_state_t g_shutdown_state = SHUTDOWN_REQUEST_STATE_UNINITIALIZED;
volatile power_ctrl_result_t g_shutdown_last_result = POWER_CTRL_RESULT_NOT_INITIALIZED;
volatile uint8_t g_shutdown_active = 0U;
volatile uint8_t g_shutdown_complete = 0U;
volatile uint8_t g_shutdown_fault = 0U;
volatile uint8_t g_pa11_level = 1U;
volatile uint8_t g_pa8_level = 0U;

volatile uint8_t pa11_shutdown_req = 0U;
volatile uint8_t pd1_usb_vbus = 0U;
volatile uint8_t shutdown_done = 0U;

volatile uint8_t g_shutdown_force_off_request = 0U;
volatile uint8_t g_shutdown_force_off_accepted = 0U;
volatile uint32_t g_shutdown_force_off_count = 0U;

volatile uint8_t g_reset_external = 0U;
volatile uint8_t g_reset_power_on = 0U;
volatile uint8_t g_reset_software = 0U;
volatile uint8_t g_reset_fwdgt = 0U;
volatile uint8_t g_reset_wwdgt = 0U;
volatile uint8_t g_reset_low_power = 0U;

volatile uint8_t g_orin_reset_output_test_request = 0U;
volatile uint8_t g_orin_reset_output_test_busy = 0U;
volatile uint8_t g_orin_reset_output_test_done = 0U;
volatile uint32_t g_orin_reset_output_test_count = 0U;
volatile power_ctrl_result_t g_orin_reset_output_test_result = POWER_CTRL_RESULT_NOT_INITIALIZED;
volatile uint8_t g_orin_reset_test_pa8_before = 1U;
volatile uint8_t g_orin_reset_test_pa8_after = 1U;
volatile uint8_t g_orin_reset_test_pb15_latch_before = 1U;
volatile uint8_t g_orin_reset_test_pb15_latch_after = 1U;

volatile uint8_t g_rtc_set_test_request = 0U;
volatile uint8_t g_rtc_set_test_busy = 0U;
volatile uint8_t g_rtc_set_test_done = 0U;
volatile uint16_t g_rtc_test_year = 2026U;
volatile uint8_t g_rtc_test_month = 8U;
volatile uint8_t g_rtc_test_day = 13U;
volatile uint8_t g_rtc_test_hour = 0U;
volatile uint8_t g_rtc_test_minute = 0U;
volatile uint8_t g_rtc_test_second = 0U;
volatile rtc_calendar_result_t g_rtc_set_test_result = RTC_CALENDAR_RESULT_NOT_INITIALIZED;


//Keil watch variables
volatile uint8_t g_system_tasks_initialized = 0U;
volatile uint32_t g_system_event_bits = 0U;
volatile uint32_t g_system_watchdog_loop_count = 0U;
volatile uint32_t g_system_power_manager_loop_count = 0U;
volatile uint32_t g_system_storage_loop_count = 0U;
volatile uint32_t g_system_orin_status_loop_count = 0U;
volatile uint32_t g_system_voltage_loop_count = 0U;
volatile uint32_t g_system_rtc_loop_count = 0U;
volatile uint32_t g_system_diagnostics_loop_count = 0U;

volatile uint32_t g_system_watchdog_stack_hwm = SYSTEM_TASK_WATCHDOG_STACK_WORDS;
volatile uint32_t g_system_power_manager_stack_hwm = SYSTEM_TASK_POWER_MANAGER_STACK_WORDS;
volatile uint32_t g_system_storage_stack_hwm = SYSTEM_TASK_STORAGE_STACK_WORDS;
volatile uint32_t g_system_orin_status_stack_hwm = SYSTEM_TASK_ORIN_STATUS_STACK_WORDS;
volatile uint32_t g_system_voltage_stack_hwm = SYSTEM_TASK_VOLTAGE_STACK_WORDS;
volatile uint32_t g_system_rtc_stack_hwm = SYSTEM_TASK_RTC_STACK_WORDS;
volatile uint32_t g_system_diagnostics_stack_hwm = SYSTEM_TASK_DIAGNOSTICS_STACK_WORDS;


volatile uint32_t g_system_power_safety_loop_count = 0U;
volatile uint32_t g_system_monitor_loop_count = 0U;
volatile uint32_t g_system_power_safety_stack_hwm = SYSTEM_TASK_POWER_MANAGER_STACK_WORDS;
volatile uint32_t g_system_monitor_stack_hwm = SYSTEM_TASK_VOLTAGE_STACK_WORDS;

volatile uint32_t g_system_power_queue_depth = 0U;
volatile uint32_t g_system_power_command_accepted_count = 0U;
volatile uint32_t g_system_power_command_rejected_count = 0U;
volatile uint32_t g_system_exti5_9_notify_count = 0U;
volatile uint32_t g_system_exti10_15_notify_count = 0U;
volatile uint32_t g_system_storage_irq_wake_count = 0U;

volatile uint8_t g_system_power_command_request = 0U;
volatile system_power_command_t g_system_last_power_command = SYSTEM_POWER_COMMAND_NONE;
volatile power_ctrl_result_t g_system_last_power_command_result = POWER_CTRL_RESULT_NOT_INITIALIZED;
volatile uint8_t g_system_diagnostics_timer_started = 0U;

static StaticTask_t s_watchdog_tcb;
static StackType_t s_watchdog_stack[SYSTEM_TASK_WATCHDOG_STACK_WORDS];
static TaskHandle_t s_watchdog_handle = (TaskHandle_t)0;

static StaticTask_t s_power_manager_tcb;
static StackType_t s_power_manager_stack[SYSTEM_TASK_POWER_MANAGER_STACK_WORDS];
static TaskHandle_t s_power_manager_handle = (TaskHandle_t)0;

static StaticTask_t s_storage_tcb;
static StackType_t s_storage_stack[SYSTEM_TASK_STORAGE_STACK_WORDS];
static TaskHandle_t s_storage_handle = (TaskHandle_t)0;

static StaticTask_t s_orin_status_tcb;
static StackType_t s_orin_status_stack[SYSTEM_TASK_ORIN_STATUS_STACK_WORDS];
static TaskHandle_t s_orin_status_handle = (TaskHandle_t)0;

static StaticTask_t s_voltage_tcb;
static StackType_t s_voltage_stack[SYSTEM_TASK_VOLTAGE_STACK_WORDS];
static TaskHandle_t s_voltage_handle = (TaskHandle_t)0;

static StaticTask_t s_rtc_tcb;
static StackType_t s_rtc_stack[SYSTEM_TASK_RTC_STACK_WORDS];
static TaskHandle_t s_rtc_handle = (TaskHandle_t)0;

static StaticTask_t s_diagnostics_tcb;
static StackType_t s_diagnostics_stack[SYSTEM_TASK_DIAGNOSTICS_STACK_WORDS];
static TaskHandle_t s_diagnostics_handle = (TaskHandle_t)0;

static StaticQueue_t s_power_command_queue_control;
static uint8_t s_power_command_queue_storage[SYSTEM_POWER_COMMAND_QUEUE_LENGTH * sizeof(system_power_command_t)];
static QueueHandle_t s_power_command_queue = (QueueHandle_t)0;

static StaticEventGroup_t s_system_event_group_control;
static EventGroupHandle_t s_system_event_group = (EventGroupHandle_t)0;

static StaticTimer_t s_diagnostics_timer_control;
static TimerHandle_t s_diagnostics_timer = (TimerHandle_t)0;



static void system_counter_increment(volatile uint32_t *counter)
{
	if (*counter != SYSTEM_COUNTER_MAX)
	{
		(*counter)++;
	}
}

static uint8_t system_reset_flag_read(rcu_flag_enum flag)
{
	return (rcu_flag_get(flag) != RESET) ? 1U : 0U;
}

void system_tasks_reset_reason_capture(void)
{
	g_reset_external = system_reset_flag_read(RCU_FLAG_EPRST);
	g_reset_power_on = system_reset_flag_read(RCU_FLAG_PORRST);
	g_reset_software = system_reset_flag_read(RCU_FLAG_SWRST);
	g_reset_fwdgt = system_reset_flag_read(RCU_FLAG_FWDGTRST);
	g_reset_wwdgt = system_reset_flag_read(RCU_FLAG_WWDGTRST);
	g_reset_low_power = system_reset_flag_read(RCU_FLAG_LPRST);
}

static uint32_t system_reset_flags_pack(void)
{
	uint32_t flags = 0U;

	if (g_reset_external != 0U)
	{
		flags |= (1UL << 0);
	}
	if (g_reset_power_on != 0U)
	{
		flags |= (1UL << 1);
	}
	if (g_reset_software != 0U)
	{
		flags |= (1UL << 2);
	}
	if (g_reset_fwdgt != 0U)
	{
		flags |= (1UL << 3);
	}
	if (g_reset_wwdgt != 0U)
	{
		flags |= (1UL << 4);
	}
	if (g_reset_low_power != 0U)
	{
		flags |= (1UL << 5);
	}

	return flags;
}

static void system_event_replace(EventBits_t mask, EventBits_t active_bits)
{
	EventBits_t clear_bits;
	clear_bits = mask & (~active_bits);

	if (clear_bits != 0U)
	{
		(void)xEventGroupClearBits(s_system_event_group, clear_bits);
	}

	if (active_bits != 0U)
	{
		(void)xEventGroupSetBits(s_system_event_group, active_bits);
	}
}

static void system_event_snapshot_update(void)
{
	g_system_event_bits = (uint32_t)xEventGroupGetBits(s_system_event_group);
}

static uint8_t system_pb15_output_latch_read(void)
{
    return (gpio_output_bit_get(ORIN_SYS_RESET_CTRL_PORT, ORIN_SYS_RESET_CTRL_PIN) != RESET) ? 1U : 0U;
}

static void system_power_fail_snapshot_refresh(void)
{
	power_fail_snapshot_t snapshot = {0U};

	snapshot.uptime_ms = systick_get_ms();
	snapshot.reset_flags = system_reset_flags_pack();
	snapshot.power_state = (uint32_t)g_power_state;
	snapshot.shutdown_state = (uint32_t)g_shutdown_state;
	snapshot.watchdog_state = (uint32_t)g_watchdog_state;
	snapshot.watchdog_feed_count = g_watchdog_feed_count;
	snapshot.voltage_fault_flags = voltage_fault_flags;
	snapshot.orin_vdd_mv = orin_vdd_mv;
	snapshot.system_5v_mv = system_5v_mv;
	snapshot.system_3v3_mv = system_3v3_mv;
	snapshot.system_1v8_mv = system_1v8_mv;
	snapshot.user_words[0] = (uint32_t)g_boot_stage;
	snapshot.user_words[1] = shutdown_request_event_count;
	snapshot.user_words[2] = g_watchdog_timeout_count;
	snapshot.user_words[3] = power_fail_event_count;
	snapshot.user_words[4] = g_shutdown_force_off_count;

	power_fail_snapshot_update(&snapshot);
}

static void system_shutdown_status_update(void)
{
	g_power_state = power_ctrl_get_state();
	g_pa8_level = (orin_status_is_initialized() != 0U) ? orin_reset_pin_state : ((orin_reset_status_is_active() != 0U) ? 0U : 1U);
	pd1_usb_vbus = usb_vbus_enable_is_active();

	if (g_shutdown_module_initialized == 0U)
	{
		g_shutdown_state = SHUTDOWN_REQUEST_STATE_UNINITIALIZED;
		g_shutdown_last_result = POWER_CTRL_RESULT_NOT_INITIALIZED;
		g_shutdown_active = 0U;
		g_shutdown_complete = 0U;
		g_shutdown_fault = 0U;
		g_pa11_level = 1U;
		pa11_shutdown_req = 0U;
		shutdown_done = 0U;
		return;
	}

	g_shutdown_active = shutdown_request_active_get();
	g_pa11_level = shutdown_request_pin_state;
	pa11_shutdown_req = g_shutdown_active;
	g_shutdown_state = shutdown_request_get_state();
	g_shutdown_last_result = shutdown_request_get_last_power_result();
	g_shutdown_complete = shutdown_request_is_complete();
	g_shutdown_fault = shutdown_request_has_fault();
	shutdown_done = g_shutdown_complete;
}

static void system_watchdog_event_refresh(void)
{
	EventBits_t bits = 0U;

	if ((external_watchdog_timeout_is_active() != 0U) || (external_watchdog_get_state() == WATCHDOG_CTRL_STATE_FAULT))
	{
		bits |= SYSTEM_EVENT_WATCHDOG_FAULT;
	}

	system_event_replace(SYSTEM_WATCHDOG_EVENT_MASK, bits);
}

static void system_power_event_refresh(void)
{
	EventBits_t bits = 0U;

	if (power_ctrl_get_state() == POWER_CTRL_STATE_ON)
	{
		bits |= SYSTEM_EVENT_POWER_ON;
	}
	if (g_shutdown_module_initialized != 0U)
	{
		bits |= SYSTEM_EVENT_SHUTDOWN_READY;
	}
	if (g_shutdown_active != 0U)
	{
		bits |= SYSTEM_EVENT_SHUTDOWN_ACTIVE;
	}
	if (g_shutdown_fault != 0U)
	{
		bits |= SYSTEM_EVENT_SHUTDOWN_FAULT;
	}

	system_event_replace(SYSTEM_POWER_EVENT_MASK, bits);
}

static void system_storage_event_refresh(void)
{
	EventBits_t bits = 0U;

	if (g_power_fail_storage_ready != 0U)
	{
		bits |= SYSTEM_EVENT_POWER_FAIL_READY;
	}
	if ((power_fail_pending_get() != 0U) || (power_fail_active_get() != 0U) || (g_power_fail_state == POWER_FAIL_STATE_SAVED))
	{
		bits |= SYSTEM_EVENT_POWER_FAIL_LATCHED;
	}

	system_event_replace(SYSTEM_STORAGE_EVENT_MASK, bits);
}

static void system_orin_event_refresh(void)
{
    EventBits_t bits = 0U;

    if ((orin_status_is_initialized() != 0U) && (orin_status_get_fault_flags() != 0U))
    {
			bits |= SYSTEM_EVENT_ORIN_FAULT;
    }

    system_event_replace(SYSTEM_ORIN_EVENT_MASK, bits);
}

static void system_voltage_event_refresh(void)
{
	EventBits_t bits = 0U;

	if (voltage_monitor_is_initialized() != 0U)
	{
		bits |= SYSTEM_EVENT_VOLTAGE_READY;
	}
	if ((voltage_monitor_is_initialized() != 0U) && (voltage_monitor_get_fault_flags() != 0U))
	{
		bits |= SYSTEM_EVENT_VOLTAGE_FAULT;
	}

	system_event_replace(SYSTEM_VOLTAGE_EVENT_MASK, bits);
}

static void system_rtc_event_refresh(void)
{
	EventBits_t bits = 0U;

	if (rtc_calendar_is_initialized() != 0U)
	{
		bits |= SYSTEM_EVENT_RTC_READY;
	}

	system_event_replace(SYSTEM_RTC_EVENT_MASK, bits);
}

static void system_watchdog_feed_request(void)
{
	if (s_watchdog_handle != (TaskHandle_t)0)
	{
		(void)xTaskNotify(s_watchdog_handle, SYSTEM_NOTIFY_WATCHDOG_FEED_NOW, eSetBits);
	}
}

static void system_shutdown_init_after_power_on(void)
{
	if ((g_power_on_result != POWER_CTRL_RESULT_OK) || (power_ctrl_get_state() != POWER_CTRL_STATE_ON))
	{
		return;
	}

	if (g_shutdown_module_initialized == 0U)
	{
		shutdown_request_init();
		g_shutdown_module_initialized = 1U;
	}
	else
	{
		(void)shutdown_request_rearm();
	}
}

static void system_startup_wait_500ms(void)
{
	uint32_t elapsed_ms;

	for (elapsed_ms = 0U; elapsed_ms < 500U; elapsed_ms++)
	{
		if (g_shutdown_module_initialized != 0U)
		{
			shutdown_request_task_1ms();
		}

		vTaskDelay(pdMS_TO_TICKS(1U));
	}
}

static void system_orin_reset_output_test_process(void)
{
    if (g_orin_reset_output_test_request !=
        SYSTEM_ORIN_RESET_OUTPUT_TEST_KEY)
    {
			return;
    }

    g_orin_reset_output_test_request = 0U;
    g_orin_reset_output_test_busy = 1U;
    g_orin_reset_output_test_done = 0U;

    g_orin_reset_test_pa8_before = (orin_reset_status_is_active() != 0U) ? 0U : 1U;
    g_orin_reset_test_pb15_latch_before = system_pb15_output_latch_read();

    g_orin_reset_output_test_result = orin_reset_pulse();

    g_orin_reset_test_pa8_after = (orin_reset_status_is_active() != 0U) ? 0U : 1U;
    g_orin_reset_test_pb15_latch_after = system_pb15_output_latch_read();
    g_power_state = power_ctrl_get_state();

    system_counter_increment(&g_orin_reset_output_test_count);
    g_orin_reset_output_test_busy = 0U;
    g_orin_reset_output_test_done = 1U;
}

static void system_shutdown_force_off_process(void)
{
	if (g_shutdown_force_off_request != SYSTEM_FORCE_OFF_REQUEST_VALUE)
	{
		return;
	}

	g_shutdown_force_off_request = 0U;
	g_shutdown_force_off_accepted = 0U;

	if ((g_shutdown_module_initialized != 0U) && (shutdown_request_has_fault() != 0U))
	{
		shutdown_request_force_off();
		g_shutdown_force_off_accepted = 1U;
		system_counter_increment(&g_shutdown_force_off_count);
	}
}

static void system_power_command_process(void)
{
	system_power_command_t command;
	power_ctrl_result_t result;

	while (xQueueReceive(s_power_command_queue, &command, 0U) == pdPASS)
	{
		g_system_last_power_command = command;
		result = POWER_CTRL_RESULT_INVALID_STATE;

		switch (command)
		{
			case SYSTEM_POWER_COMMAND_ORIN_POWER_ON:
				result = orin_power_on();
				g_power_on_result = result;
				if (result == POWER_CTRL_RESULT_OK)
				{
					system_shutdown_init_after_power_on();
					system_watchdog_feed_request();
					(void)xEventGroupSetBits(s_system_event_group, SYSTEM_EVENT_POWER_ON);
				}
				break;

			case SYSTEM_POWER_COMMAND_ORIN_RESET:
				result = orin_reset_pulse();
				break;

			case SYSTEM_POWER_COMMAND_FORCE_OFF:
				power_ctrl_force_off();
				result = POWER_CTRL_RESULT_OK;
				(void)xEventGroupClearBits(s_system_event_group, SYSTEM_EVENT_POWER_ON);
				break;

			case SYSTEM_POWER_COMMAND_NONE:
			default:
				result = POWER_CTRL_RESULT_INVALID_STATE;
				break;
		}

		g_system_last_power_command_result = result;

		if (result == POWER_CTRL_RESULT_OK)
		{
			system_counter_increment(&g_system_power_command_accepted_count);
		}
		else
		{
			system_counter_increment(&g_system_power_command_rejected_count);
		}

		g_power_state = power_ctrl_get_state();
	}
}

static void system_power_command_watch_request_process(void)
{
	uint8_t request;

	request = g_system_power_command_request;
	if (request == (uint8_t)SYSTEM_POWER_COMMAND_NONE)
	{
		return;
	}

	g_system_power_command_request = (uint8_t)SYSTEM_POWER_COMMAND_NONE;

	if (request > (uint8_t)SYSTEM_POWER_COMMAND_FORCE_OFF)
	{
		system_counter_increment(&g_system_power_command_rejected_count);
		return;
	}

	(void)system_tasks_power_command_send((system_power_command_t)request, 0U);
}

static void system_rtc_set_test_process(void)
{
	rtc_calendar_datetime_t datetime = {0U};

	if (g_rtc_set_test_request != SYSTEM_RTC_SET_TEST_KEY)
	{
		return;
	}

	g_rtc_set_test_request = 0U;
	g_rtc_set_test_busy = 1U;
	g_rtc_set_test_done = 0U;

	datetime.year = g_rtc_test_year;
	datetime.month = g_rtc_test_month;
	datetime.day = g_rtc_test_day;
	datetime.hour = g_rtc_test_hour;
	datetime.minute = g_rtc_test_minute;
	datetime.second = g_rtc_test_second;

	g_rtc_set_test_result = rtc_calendar_set_datetime(&datetime);

	g_rtc_set_test_busy = 0U;
	g_rtc_set_test_done = 1U;
}

static void system_watchdog_task(void *argument)
{
	TickType_t last_wake_time;
	uint32_t notification_value;

	(void)argument;

	(void)xEventGroupWaitBits(s_system_event_group, SYSTEM_EVENT_GPIO_READY, pdFALSE, pdTRUE, portMAX_DELAY);

	g_watchdog_init_result = external_watchdog_init();
	(void)xEventGroupSetBits(s_system_event_group, SYSTEM_EVENT_WATCHDOG_INIT_DONE);

	if (g_watchdog_init_result != WATCHDOG_CTRL_RESULT_OK)
	{
		g_boot_stage = SYSTEM_BOOT_STAGE_WATCHDOG_FAULT;
		(void)xEventGroupSetBits(s_system_event_group, SYSTEM_EVENT_WATCHDOG_FAULT);
	}

	last_wake_time = xTaskGetTickCount();

	while (1)
	{
		notification_value = 0U;
		(void)xTaskNotifyWait(0U, SYSTEM_COUNTER_MAX, &notification_value, 0U);

		if ((notification_value & SYSTEM_NOTIFY_WATCHDOG_FEED_NOW) != 0U)
		{
			g_watchdog_manual_feed_result = external_watchdog_feed_now();
		}

		if (external_watchdog_is_initialized() != 0U)
		{
			external_watchdog_task_1ms();
		}

		system_counter_increment(&g_system_watchdog_loop_count);
		system_watchdog_event_refresh();

		vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(SYSTEM_WATCHDOG_TASK_PERIOD_MS));
	}
}

static void system_power_manager_task(void *argument)
{
	EventBits_t init_bits;
	uint32_t notification_value;

	(void)argument;

	g_boot_stage = 1U;
	power_gpio_init();
	g_power_state = power_ctrl_get_state();
	(void)xEventGroupSetBits(s_system_event_group, SYSTEM_EVENT_GPIO_READY);

	g_boot_stage = 2U;
	(void)xEventGroupWaitBits(s_system_event_group, SYSTEM_EVENT_WATCHDOG_INIT_DONE, pdFALSE, pdTRUE, portMAX_DELAY);

	if (g_watchdog_init_result != WATCHDOG_CTRL_RESULT_OK)
	{
		g_boot_stage = SYSTEM_BOOT_STAGE_WATCHDOG_FAULT;
	}

	g_boot_stage = 3U;
	init_bits = xEventGroupWaitBits(s_system_event_group, SYSTEM_EVENT_STORAGE_INIT_DONE | SYSTEM_EVENT_ORIN_STATUS_INIT_DONE, pdFALSE, pdTRUE, portMAX_DELAY);
	(void)init_bits;

	g_boot_stage = 4U;
	g_power_on_result = orin_power_on();
	g_power_state = power_ctrl_get_state();
	system_watchdog_feed_request();

	system_shutdown_init_after_power_on();

	g_boot_stage = 5U;
	if (g_power_on_result == POWER_CTRL_RESULT_OK)
	{
		system_startup_wait_500ms();
		system_watchdog_feed_request();
		(void)xEventGroupSetBits(s_system_event_group, SYSTEM_EVENT_POWER_ON);
	}
	else
	{
		g_boot_stage = SYSTEM_BOOT_STAGE_POWER_ON_FAULT;
	}

	g_boot_stage = 6U;
	if (g_power_on_result == POWER_CTRL_RESULT_OK)
	{
		(void)xEventGroupWaitBits(s_system_event_group, SYSTEM_EVENT_VOLTAGE_INIT_DONE | SYSTEM_EVENT_RTC_INIT_DONE, pdFALSE, pdTRUE, portMAX_DELAY);
	}

	system_shutdown_status_update();
	system_power_event_refresh();
	system_power_fail_snapshot_refresh();

	if (g_power_on_result == POWER_CTRL_RESULT_OK)
	{
		g_boot_stage = 8U;
	}

	(void)xEventGroupSetBits(s_system_event_group, SYSTEM_EVENT_STARTUP_COMPLETE);

	while (1)
	{
		notification_value = 0U;
		(void)xTaskNotifyWait(0U, SYSTEM_COUNTER_MAX, &notification_value, pdMS_TO_TICKS(SYSTEM_POWER_MANAGER_PERIOD_MS));
		(void)notification_value;

		freertos_app_heartbeat();
		system_counter_increment(&g_system_power_manager_loop_count);
		g_system_power_safety_loop_count = g_system_power_manager_loop_count;

		if (g_shutdown_module_initialized != 0U)
		{
			shutdown_request_task_1ms();
		}

		system_shutdown_force_off_process();
		system_orin_reset_output_test_process();
		system_power_command_watch_request_process();
		system_power_command_process();
		system_shutdown_status_update();
		system_power_event_refresh();
	}
}

static void system_storage_task(void *argument)
{
	uint32_t notification_value;

	(void)argument;
	(void)xEventGroupWaitBits(s_system_event_group, SYSTEM_EVENT_ORIN_STATUS_INIT_DONE, pdFALSE, pdTRUE, portMAX_DELAY);

	g_power_fail_init_result = power_fail_init();
	system_power_fail_snapshot_refresh();
	system_storage_event_refresh();
	(void)xEventGroupSetBits(s_system_event_group, SYSTEM_EVENT_STORAGE_INIT_DONE);

	while (1)
	{
		notification_value = 0U;
		(void)xTaskNotifyWait(0U, SYSTEM_COUNTER_MAX, &notification_value, pdMS_TO_TICKS(SYSTEM_STORAGE_TASK_PERIOD_MS));

		if ((notification_value & SYSTEM_NOTIFY_STORAGE_IRQ) != 0U)
		{
			system_counter_increment(&g_system_storage_irq_wake_count);
		}

		system_counter_increment(&g_system_storage_loop_count);
		system_power_fail_snapshot_refresh();
		power_fail_task();
		system_storage_event_refresh();
	}
}

static void system_orin_status_task(void *argument)
{
	TickType_t last_wake_time;

	(void)argument;

	(void)xEventGroupWaitBits(s_system_event_group, SYSTEM_EVENT_WATCHDOG_INIT_DONE, pdFALSE, pdTRUE, portMAX_DELAY);

	orin_status_init();
	system_orin_event_refresh();
	(void)xEventGroupSetBits(s_system_event_group, SYSTEM_EVENT_ORIN_STATUS_INIT_DONE);

	last_wake_time = xTaskGetTickCount();

	while (1)
	{
		system_counter_increment(&g_system_orin_status_loop_count);
		orin_status_task_1ms();
		system_orin_event_refresh();

		vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(SYSTEM_ORIN_STATUS_PERIOD_MS));
	}
}

static void system_voltage_task(void *argument)
{
	TickType_t last_wake_time;
	power_ctrl_state_t power_state;

	(void)argument;

	(void)xEventGroupWaitBits(s_system_event_group, SYSTEM_EVENT_POWER_ON, pdFALSE, pdTRUE, portMAX_DELAY);

	voltage_monitor_init();
	system_voltage_event_refresh();
	(void)xEventGroupSetBits(s_system_event_group, SYSTEM_EVENT_VOLTAGE_INIT_DONE);

	last_wake_time = xTaskGetTickCount();

	while (1)
	{
		system_counter_increment(&g_system_voltage_loop_count);
		g_system_monitor_loop_count = g_system_voltage_loop_count;

		power_state = power_ctrl_get_state();
		if (power_state == POWER_CTRL_STATE_ON)
		{
			voltage_monitor_set_enabled(1U);
			(void)voltage_monitor_run_cycle();
		}
		else
		{
			voltage_monitor_set_enabled(0U);
		}

		system_voltage_event_refresh();

		vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(SYSTEM_VOLTAGE_PERIOD_MS));
	}
}

static void system_rtc_task(void *argument)
{
	TickType_t last_wake_time;

	(void)argument;

	(void)xEventGroupWaitBits(s_system_event_group, SYSTEM_EVENT_VOLTAGE_INIT_DONE, pdFALSE, pdTRUE, portMAX_DELAY);

	(void)rtc_calendar_init();
	system_rtc_event_refresh();
	(void)xEventGroupSetBits(s_system_event_group, SYSTEM_EVENT_RTC_INIT_DONE);

	last_wake_time = xTaskGetTickCount();

	while (1)
	{
		system_counter_increment(&g_system_rtc_loop_count);
		system_rtc_set_test_process();

		if (rtc_calendar_is_initialized() != 0U)
		{
			(void)rtc_calendar_refresh_now();
		}

		system_rtc_event_refresh();

		vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(SYSTEM_RTC_PERIOD_MS));
	}
}

static void system_diagnostics_task(void *argument)
{
	(void)argument;

	(void)xEventGroupWaitBits(s_system_event_group, SYSTEM_EVENT_STARTUP_COMPLETE, pdFALSE, pdTRUE, portMAX_DELAY);

	if (xTimerStart(s_diagnostics_timer, 0U) == pdPASS)
	{
		g_system_diagnostics_timer_started = 1U;
	}

	while (1)
	{
		(void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		system_counter_increment(&g_system_diagnostics_loop_count);

		g_system_watchdog_stack_hwm = (uint32_t)uxTaskGetStackHighWaterMark(s_watchdog_handle);
		g_system_power_manager_stack_hwm = (uint32_t)uxTaskGetStackHighWaterMark(s_power_manager_handle);
		g_system_storage_stack_hwm = (uint32_t)uxTaskGetStackHighWaterMark(s_storage_handle);
		g_system_orin_status_stack_hwm = (uint32_t)uxTaskGetStackHighWaterMark(s_orin_status_handle);
		g_system_voltage_stack_hwm = (uint32_t)uxTaskGetStackHighWaterMark(s_voltage_handle);
		g_system_rtc_stack_hwm = (uint32_t)uxTaskGetStackHighWaterMark(s_rtc_handle);
		g_system_diagnostics_stack_hwm = (uint32_t)uxTaskGetStackHighWaterMark(s_diagnostics_handle);

		g_system_power_safety_stack_hwm = g_system_power_manager_stack_hwm;
		g_system_monitor_stack_hwm = g_system_voltage_stack_hwm;
		g_system_power_queue_depth = (uint32_t)uxQueueMessagesWaiting(s_power_command_queue);
		system_event_snapshot_update();
	}
}

static void system_diagnostics_timer_callback(TimerHandle_t timer)
{
	(void)timer;

	if (s_diagnostics_handle != (TaskHandle_t)0)
	{
		xTaskNotifyGive(s_diagnostics_handle);
	}
}

BaseType_t system_tasks_power_command_send(system_power_command_t command, TickType_t wait_ticks)
{
	if ((s_power_command_queue == (QueueHandle_t)0) || (command <= SYSTEM_POWER_COMMAND_NONE) || (command > SYSTEM_POWER_COMMAND_FORCE_OFF))
	{
		return pdFAIL;
	}

	return xQueueSendToBack(s_power_command_queue, &command, wait_ticks);
}

void system_tasks_notify_shutdown_from_isr(BaseType_t *higher_priority_woken)
{
	if (s_power_manager_handle != (TaskHandle_t)0)
	{
		(void)xTaskNotifyFromISR(s_power_manager_handle, SYSTEM_NOTIFY_POWER_MANAGER_IRQ, eSetBits, higher_priority_woken);
	}
}

void system_tasks_notify_power_fail_from_isr(BaseType_t *higher_priority_woken)
{
	if (s_storage_handle != (TaskHandle_t)0)
	{
		(void)xTaskNotifyFromISR(s_storage_handle, SYSTEM_NOTIFY_STORAGE_IRQ, eSetBits, higher_priority_woken);
	}
}

static void system_tasks_notify_watchdog_from_isr(BaseType_t *higher_priority_woken)
{
	if (s_watchdog_handle != (TaskHandle_t)0)
	{
		(void)xTaskNotifyFromISR(s_watchdog_handle, SYSTEM_NOTIFY_WATCHDOG_IRQ, eSetBits, higher_priority_woken);
	}
}

void EXTI5_9_IRQHandler(void)
{
	BaseType_t higher_priority_woken = pdFALSE;
	uint8_t power_fail_irq_pending;
	uint8_t watchdog_irq_pending;

	power_fail_irq_pending = (exti_interrupt_flag_get(POWER_FAIL_EXTI_LINE) != RESET) ? 1U : 0U;
	watchdog_irq_pending = (exti_interrupt_flag_get(WATCHDOG_WDO_EXTI_LINE) != RESET) ? 1U : 0U;

	power_fail_shared_exti5_9_irq_handler();
	external_watchdog_wdo_irq_handler();

	if (power_fail_irq_pending != 0U)
	{
		system_tasks_notify_power_fail_from_isr(&higher_priority_woken);
	}

	if (watchdog_irq_pending != 0U)
	{
		system_tasks_notify_watchdog_from_isr(&higher_priority_woken);
	}

	if ((power_fail_irq_pending != 0U) || (watchdog_irq_pending != 0U))
	{
		system_counter_increment(&g_system_exti5_9_notify_count);
	}

	portYIELD_FROM_ISR(higher_priority_woken);
}

void EXTI10_15_IRQHandler(void)
{
	BaseType_t higher_priority_woken = pdFALSE;
	uint8_t shutdown_irq_pending;

	shutdown_irq_pending = (exti_interrupt_flag_get(SHUTDOWN_REQ_EXTI_LINE) != RESET) ? 1U : 0U;

	shutdown_request_irq_handler();

	if (shutdown_irq_pending != 0U)
	{
		system_tasks_notify_shutdown_from_isr(&higher_priority_woken);
		system_counter_increment(&g_system_exti10_15_notify_count);
	}

	portYIELD_FROM_ISR(higher_priority_woken);
}

BaseType_t system_tasks_create(void)
{
	if (g_system_tasks_initialized != 0U)
	{
		return pdPASS;
	}

	s_system_event_group = xEventGroupCreateStatic(&s_system_event_group_control);
	if (s_system_event_group == (EventGroupHandle_t)0)
	{
		return pdFAIL;
	}

	s_power_command_queue = xQueueCreateStatic(SYSTEM_POWER_COMMAND_QUEUE_LENGTH, sizeof(system_power_command_t), s_power_command_queue_storage, &s_power_command_queue_control);
	if (s_power_command_queue == (QueueHandle_t)0)
	{
		return pdFAIL;
	}

	s_diagnostics_timer = xTimerCreateStatic("DiagTimer", pdMS_TO_TICKS(SYSTEM_DIAGNOSTICS_TIMER_PERIOD_MS), pdTRUE, (void *)0, system_diagnostics_timer_callback, &s_diagnostics_timer_control);
	if (s_diagnostics_timer == (TimerHandle_t)0)
	{
		return pdFAIL;
	}

	s_watchdog_handle = xTaskCreateStatic(system_watchdog_task, "Watchdog", SYSTEM_TASK_WATCHDOG_STACK_WORDS, (void *)0, SYSTEM_TASK_WATCHDOG_PRIORITY, s_watchdog_stack, &s_watchdog_tcb);
	s_power_manager_handle = xTaskCreateStatic(system_power_manager_task, "PowerManager", SYSTEM_TASK_POWER_MANAGER_STACK_WORDS, (void *)0, SYSTEM_TASK_POWER_MANAGER_PRIORITY, s_power_manager_stack, &s_power_manager_tcb);
	s_storage_handle = xTaskCreateStatic(system_storage_task, "Storage", SYSTEM_TASK_STORAGE_STACK_WORDS, (void *)0, SYSTEM_TASK_STORAGE_PRIORITY, s_storage_stack, &s_storage_tcb);
	s_orin_status_handle = xTaskCreateStatic(system_orin_status_task, "OrinStatus", SYSTEM_TASK_ORIN_STATUS_STACK_WORDS, (void *)0, SYSTEM_TASK_ORIN_STATUS_PRIORITY, s_orin_status_stack, &s_orin_status_tcb);
	s_voltage_handle = xTaskCreateStatic(system_voltage_task, "Voltage", SYSTEM_TASK_VOLTAGE_STACK_WORDS, (void *)0, SYSTEM_TASK_VOLTAGE_PRIORITY, s_voltage_stack, &s_voltage_tcb);
	s_rtc_handle = xTaskCreateStatic(system_rtc_task, "RTC", SYSTEM_TASK_RTC_STACK_WORDS, (void *)0, SYSTEM_TASK_RTC_PRIORITY, s_rtc_stack, &s_rtc_tcb);
	s_diagnostics_handle = xTaskCreateStatic(system_diagnostics_task, "Diagnostics", SYSTEM_TASK_DIAGNOSTICS_STACK_WORDS, (void *)0, SYSTEM_TASK_DIAGNOSTICS_PRIORITY, s_diagnostics_stack, &s_diagnostics_tcb);

	if ((s_watchdog_handle == (TaskHandle_t)0) || (s_power_manager_handle == (TaskHandle_t)0) || (s_storage_handle == (TaskHandle_t)0) || (s_orin_status_handle == (TaskHandle_t)0) || (s_voltage_handle == (TaskHandle_t)0) || (s_rtc_handle == (TaskHandle_t)0) || (s_diagnostics_handle == (TaskHandle_t)0))
	{
		return pdFAIL;
	}

	vQueueAddToRegistry(s_power_command_queue, "PowerCommand");
	g_system_tasks_initialized = 1U;

	return pdPASS;
}
