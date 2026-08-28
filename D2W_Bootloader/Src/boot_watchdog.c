#include "boot_watchdog.h"

#include "boot_config.h"
#include "boot_time.h"

volatile uint8_t g_boot_watchdog_initialized = 0U;
volatile uint8_t g_boot_watchdog_wdi_level = 0U;
volatile uint32_t g_boot_watchdog_feed_count = 0UL;

static uint32_t s_last_feed_ms = 0UL;

static void boot_watchdog_toggle(void)
{
	if (g_boot_watchdog_wdi_level == 0U)
	{
		gpio_bit_set(BOOT_WATCHDOG_PORT, BOOT_WATCHDOG_PIN);
		g_boot_watchdog_wdi_level = 1U;
	}
	else
	{
		gpio_bit_reset(BOOT_WATCHDOG_PORT, BOOT_WATCHDOG_PIN);
		g_boot_watchdog_wdi_level = 0U;
	}

	if (g_boot_watchdog_feed_count != 0xFFFFFFFFUL)
	{
		g_boot_watchdog_feed_count++;
	}

	s_last_feed_ms = boot_time_now();
}

void boot_watchdog_init(void)
{
	rcu_periph_clock_enable(BOOT_WATCHDOG_GPIO_RCU);

	gpio_bit_reset(BOOT_WATCHDOG_PORT, BOOT_WATCHDOG_PIN);
	gpio_init(BOOT_WATCHDOG_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_2MHZ, BOOT_WATCHDOG_PIN);

	g_boot_watchdog_wdi_level = 0U;
	boot_time_delay_ms(BOOT_WATCHDOG_FIRST_LOW_MS);

	gpio_bit_set(BOOT_WATCHDOG_PORT, BOOT_WATCHDOG_PIN);
	g_boot_watchdog_wdi_level = 1U;
	g_boot_watchdog_feed_count = 1UL;
	s_last_feed_ms = boot_time_now();

	boot_time_delay_ms(BOOT_WATCHDOG_FIRST_HIGH_MS);
	g_boot_watchdog_initialized = 1U;
}

void boot_watchdog_service(void)
{
	if (g_boot_watchdog_initialized == 0U)
	{
		return;
	}

	if (boot_time_elapsed(s_last_feed_ms, BOOT_WATCHDOG_FEED_PERIOD_MS) != 0U)
	{
		boot_watchdog_toggle();
	}
}

void boot_watchdog_feed_now(void)
{
	if (g_boot_watchdog_initialized != 0U)
	{
		boot_watchdog_toggle();
	}
}
