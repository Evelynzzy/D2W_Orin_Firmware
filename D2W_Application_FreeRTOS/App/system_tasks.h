#ifndef SYSTEM_TASKS_H
#define SYSTEM_TASKS_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#include "power_ctrl.h"
#include "rtc_calendar.h"
#include "shutdown_request.h"
#include "watchdog_ctrl.h"

#ifdef __cplusplus
extern "C" {
#endif

#if (configMAX_PRIORITIES < 7U)
#error "configMAX_PRIORITIES must be at least 7"
#endif

#define SYSTEM_TASK_WATCHDOG_STACK_WORDS    (256U)
#define SYSTEM_TASK_POWER_MANAGER_STACK_WORDS    (1024U)
#define SYSTEM_TASK_STORAGE_STACK_WORDS    (640U)
#define SYSTEM_TASK_ORIN_STATUS_STACK_WORDS    (384U)
#define SYSTEM_TASK_VOLTAGE_STACK_WORDS    (512U)
#define SYSTEM_TASK_RTC_STACK_WORDS    (640U)
#define SYSTEM_TASK_DIAGNOSTICS_STACK_WORDS    (384U)


#define SYSTEM_TASK_STORAGE_PRIORITY    (configMAX_PRIORITIES - 1U)
#define SYSTEM_TASK_WATCHDOG_PRIORITY    (configMAX_PRIORITIES - 2U)
#define SYSTEM_TASK_POWER_MANAGER_PRIORITY    (configMAX_PRIORITIES - 3U)
#define SYSTEM_TASK_ORIN_STATUS_PRIORITY    (configMAX_PRIORITIES - 4U)
#define SYSTEM_TASK_VOLTAGE_PRIORITY    (configMAX_PRIORITIES - 5U)
#define SYSTEM_TASK_RTC_PRIORITY    (configMAX_PRIORITIES - 5U)
#define SYSTEM_TASK_DIAGNOSTICS_PRIORITY    (configMAX_PRIORITIES - 6U)

#define SYSTEM_POWER_COMMAND_QUEUE_LENGTH    (4U)

#define SYSTEM_EVENT_GPIO_READY    (1UL << 0)
#define SYSTEM_EVENT_WATCHDOG_INIT_DONE    (1UL << 1)
#define SYSTEM_EVENT_STORAGE_INIT_DONE    (1UL << 2)
#define SYSTEM_EVENT_ORIN_STATUS_INIT_DONE    (1UL << 3)
#define SYSTEM_EVENT_POWER_ON    (1UL << 4)
#define SYSTEM_EVENT_SHUTDOWN_READY    (1UL << 5)
#define SYSTEM_EVENT_VOLTAGE_INIT_DONE    (1UL << 6)
#define SYSTEM_EVENT_RTC_INIT_DONE    (1UL << 7)
#define SYSTEM_EVENT_STARTUP_COMPLETE    (1UL << 8)
#define SYSTEM_EVENT_SHUTDOWN_ACTIVE    (1UL << 9)
#define SYSTEM_EVENT_WATCHDOG_FAULT    (1UL << 10)
#define SYSTEM_EVENT_ORIN_FAULT    (1UL << 11)
#define SYSTEM_EVENT_VOLTAGE_FAULT    (1UL << 12)
#define SYSTEM_EVENT_POWER_FAIL_LATCHED    (1UL << 13)
#define SYSTEM_EVENT_SHUTDOWN_FAULT    (1UL << 14)
#define SYSTEM_EVENT_POWER_FAIL_READY    (1UL << 15)
#define SYSTEM_EVENT_VOLTAGE_READY    (1UL << 16)
#define SYSTEM_EVENT_RTC_READY    (1UL << 17)

typedef enum
{
	SYSTEM_POWER_COMMAND_NONE = 0,
	SYSTEM_POWER_COMMAND_ORIN_POWER_ON,
	SYSTEM_POWER_COMMAND_ORIN_RESET,
	SYSTEM_POWER_COMMAND_FORCE_OFF
} system_power_command_t;


extern volatile uint8_t g_boot_stage;

extern volatile power_ctrl_result_t g_power_on_result;
extern volatile power_ctrl_state_t g_power_state;

extern volatile watchdog_ctrl_result_t g_watchdog_init_result;
extern volatile watchdog_ctrl_result_t g_watchdog_manual_feed_result;

extern volatile uint8_t g_shutdown_module_initialized;
extern volatile shutdown_request_state_t g_shutdown_state;
extern volatile power_ctrl_result_t g_shutdown_last_result;
extern volatile uint8_t g_shutdown_active;
extern volatile uint8_t g_shutdown_complete;
extern volatile uint8_t g_shutdown_fault;
extern volatile uint8_t g_pa11_level;
extern volatile uint8_t g_pa8_level;

extern volatile uint8_t pa11_shutdown_req;
extern volatile uint8_t pd1_usb_vbus;
extern volatile uint8_t shutdown_done;

extern volatile uint8_t g_shutdown_force_off_request;
extern volatile uint8_t g_shutdown_force_off_accepted;
extern volatile uint32_t g_shutdown_force_off_count;

extern volatile uint8_t g_reset_external;
extern volatile uint8_t g_reset_power_on;
extern volatile uint8_t g_reset_software;
extern volatile uint8_t g_reset_fwdgt;
extern volatile uint8_t g_reset_wwdgt;
extern volatile uint8_t g_reset_low_power;

extern volatile uint8_t g_orin_reset_output_test_request;
extern volatile uint8_t g_orin_reset_output_test_busy;
extern volatile uint8_t g_orin_reset_output_test_done;
extern volatile uint32_t g_orin_reset_output_test_count;
extern volatile power_ctrl_result_t g_orin_reset_output_test_result;
extern volatile uint8_t g_orin_reset_test_pa8_before;
extern volatile uint8_t g_orin_reset_test_pa8_after;
extern volatile uint8_t g_orin_reset_test_pb15_latch_before;
extern volatile uint8_t g_orin_reset_test_pb15_latch_after;

extern volatile uint8_t g_rtc_set_test_request;
extern volatile uint8_t g_rtc_set_test_busy;
extern volatile uint8_t g_rtc_set_test_done;
extern volatile uint16_t g_rtc_test_year;
extern volatile uint8_t g_rtc_test_month;
extern volatile uint8_t g_rtc_test_day;
extern volatile uint8_t g_rtc_test_hour;
extern volatile uint8_t g_rtc_test_minute;
extern volatile uint8_t g_rtc_test_second;
extern volatile rtc_calendar_result_t g_rtc_set_test_result;

extern volatile uint8_t g_system_tasks_initialized;
extern volatile uint32_t g_system_event_bits;
extern volatile uint32_t g_system_watchdog_loop_count;
extern volatile uint32_t g_system_power_manager_loop_count;
extern volatile uint32_t g_system_storage_loop_count;
extern volatile uint32_t g_system_orin_status_loop_count;
extern volatile uint32_t g_system_voltage_loop_count;
extern volatile uint32_t g_system_rtc_loop_count;
extern volatile uint32_t g_system_diagnostics_loop_count;

extern volatile uint32_t g_system_watchdog_stack_hwm;
extern volatile uint32_t g_system_power_manager_stack_hwm;
extern volatile uint32_t g_system_storage_stack_hwm;
extern volatile uint32_t g_system_orin_status_stack_hwm;
extern volatile uint32_t g_system_voltage_stack_hwm;
extern volatile uint32_t g_system_rtc_stack_hwm;
extern volatile uint32_t g_system_diagnostics_stack_hwm;

extern volatile uint32_t g_system_power_safety_loop_count;
extern volatile uint32_t g_system_monitor_loop_count;
extern volatile uint32_t g_system_power_safety_stack_hwm;
extern volatile uint32_t g_system_monitor_stack_hwm;

extern volatile uint32_t g_system_power_queue_depth;
extern volatile uint32_t g_system_power_command_accepted_count;
extern volatile uint32_t g_system_power_command_rejected_count;
extern volatile uint32_t g_system_exti5_9_notify_count;
extern volatile uint32_t g_system_exti10_15_notify_count;
extern volatile uint32_t g_system_storage_irq_wake_count;


extern volatile uint8_t g_system_power_command_request;
extern volatile system_power_command_t g_system_last_power_command;
extern volatile power_ctrl_result_t g_system_last_power_command_result;
extern volatile uint8_t g_system_diagnostics_timer_started;


void system_tasks_reset_reason_capture(void);

BaseType_t system_tasks_create(void);

BaseType_t system_tasks_power_command_send(system_power_command_t command, TickType_t wait_ticks);

void system_tasks_notify_shutdown_from_isr(BaseType_t *higher_priority_woken);
void system_tasks_notify_power_fail_from_isr(BaseType_t *higher_priority_woken);

#ifdef __cplusplus
}
#endif

#endif 
