#include "systick.h"

#include "FreeRTOS.h"
#include "task.h"

void systick_config(void)
{
}

uint32_t systick_get_ms(void)
{
	TickType_t ticks = xTaskGetTickCount();
	return (uint32_t)(ticks * (TickType_t)portTICK_PERIOD_MS);
}

uint32_t systick_get_ms_from_isr(void)
{
	TickType_t ticks = xTaskGetTickCountFromISR();
	return (uint32_t)(ticks * (TickType_t)portTICK_PERIOD_MS);
}

void delay_1ms(uint32_t count)
{
	TickType_t delay_ticks;

	if (count == 0U)
	{
		return;
	}

	delay_ticks = pdMS_TO_TICKS(count);

	if (delay_ticks == (TickType_t)0)
	{
		delay_ticks = (TickType_t)1;
	}

	vTaskDelay(delay_ticks);
}
