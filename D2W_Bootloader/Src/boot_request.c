#include "boot_request.h"

#include "boot_config.h"

volatile uint8_t g_boot_update_requested = 0U;

static void boot_request_backup_access_enable(void)
{
	rcu_periph_clock_enable(RCU_PMU);
	rcu_periph_clock_enable(RCU_BKPI);
	pmu_backup_write_enable();
}

void boot_request_clear(void)
{
	boot_request_backup_access_enable();
	bkp_write_data(BOOT_REQUEST_REGISTER_HIGH, 0U);
	bkp_write_data(BOOT_REQUEST_REGISTER_LOW, 0U);
	pmu_backup_write_disable();
}

uint8_t boot_request_take(void)
{
	uint16_t high_word;
	uint16_t low_word;

	boot_request_backup_access_enable();
	high_word = bkp_read_data(BOOT_REQUEST_REGISTER_HIGH);
	low_word = bkp_read_data(BOOT_REQUEST_REGISTER_LOW);

	if ((high_word == BOOT_REQUEST_MAGIC_HIGH) && (low_word == BOOT_REQUEST_MAGIC_LOW))
	{
		bkp_write_data(BOOT_REQUEST_REGISTER_HIGH, 0U);
		bkp_write_data(BOOT_REQUEST_REGISTER_LOW, 0U);
		pmu_backup_write_disable();
		g_boot_update_requested = 1U;
		return 1U;
	}

	pmu_backup_write_disable();
	g_boot_update_requested = 0U;
	return 0U;
}
