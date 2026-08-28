#ifndef FREERTOS_APP_H
#define FREERTOS_APP_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif


#define FREERTOS_APP_IDLE_STACK_WORDS    configMINIMAL_STACK_SIZE
#define FREERTOS_APP_TIMER_STACK_WORDS    (256U)

typedef enum
{
	FREERTOS_APP_FAULT_NONE = 0,
	FREERTOS_APP_FAULT_OBJECT_CREATE,
	FREERTOS_APP_FAULT_TASK_CREATE,
	FREERTOS_APP_FAULT_SCHEDULER_RETURNED,
	FREERTOS_APP_FAULT_ASSERT,
	FREERTOS_APP_FAULT_STACK_OVERFLOW
} freertos_app_fault_t;

extern volatile uint8_t g_freertos_scheduler_running;
extern volatile uint32_t g_freertos_heartbeat_count;
extern volatile freertos_app_fault_t g_freertos_fault_code;
extern volatile uint32_t g_freertos_fault_line;
extern volatile uint32_t g_freertos_fault_task_handle;

void freertos_app_heartbeat(void);
void freertos_app_fatal(freertos_app_fault_t fault);
void freertos_assert_failed(const char *file, uint32_t line);

#ifdef __cplusplus
}
#endif

#endif 
