#include "boot_time.h"

#include "gd32f30x.h"

volatile uint32_t g_boot_time_ms = 0UL;
volatile uint8_t g_boot_time_initialized = 0U;

void boot_time_init(void)
{
	g_boot_time_ms = 0UL;

	if (SysTick_Config(SystemCoreClock / 1000UL) == 0UL)
	{
		g_boot_time_initialized = 1U;
	}
	else
	{
		g_boot_time_initialized = 0U;
	}
}

uint32_t boot_time_now(void)
{
	return g_boot_time_ms;
}

uint8_t boot_time_elapsed(uint32_t start_ms, uint32_t interval_ms)
{
	return (((uint32_t)(boot_time_now() - start_ms)) >= interval_ms) ? 1U : 0U;
}

void boot_time_delay_ms(uint32_t delay_ms)
{
	uint32_t start_ms = boot_time_now();

	while (boot_time_elapsed(start_ms, delay_ms) == 0U)
	{
		__NOP();
	}
}

void SysTick_Handler(void)
{
	g_boot_time_ms++;
}
