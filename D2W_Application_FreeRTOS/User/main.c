#include "gd32f30x.h"
#include "gd32f30x_misc.h"
#include "gd32f30x_rcu.h"

#include "FreeRTOS.h"
#include "task.h"

#include "app_boot_control.h"
#include "freertos_app.h"
#include "system_tasks.h"

int main(void)
{
	BaseType_t create_result;

	app_boot_control_vector_init();
	system_tasks_reset_reason_capture();
	rcu_all_reset_flag_clear();

	nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);

	create_result = system_tasks_create();
	
	if (create_result != pdPASS)
	{
		freertos_app_fatal(FREERTOS_APP_FAULT_TASK_CREATE);
	}

	vTaskStartScheduler();
	freertos_app_fatal(FREERTOS_APP_FAULT_SCHEDULER_RETURNED);
	return 0;
}
