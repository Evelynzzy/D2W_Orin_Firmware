#include "app_boot_control.h"

#include "gd32f30x.h"
#include "gd32f30x_bkp.h"
#include "gd32f30x_pmu.h"
#include "gd32f30x_rcu.h"

#include "boot_shared.h"

#define APP_BOOT_REQUEST_REGISTER_HIGH    BKP_DATA_40
#define APP_BOOT_REQUEST_REGISTER_LOW    BKP_DATA_41

void app_boot_control_vector_init(void)
{
	SCB->VTOR = BOOT_APPLICATION_ADDRESS;
	__DSB();
	__ISB();
}

void app_boot_control_mark_update_request(void)
{
	rcu_periph_clock_enable(RCU_PMU);
	rcu_periph_clock_enable(RCU_BKPI);
	pmu_backup_write_enable();

	bkp_write_data(APP_BOOT_REQUEST_REGISTER_HIGH, BOOT_REQUEST_MAGIC_HIGH);
	bkp_write_data(APP_BOOT_REQUEST_REGISTER_LOW, BOOT_REQUEST_MAGIC_LOW);

	pmu_backup_write_disable();
	__DSB();
}

void app_boot_control_request_update_and_reset(void)
{
	__disable_irq();
	app_boot_control_mark_update_request();
	NVIC_SystemReset();

	while (1)
	{
	}
}
