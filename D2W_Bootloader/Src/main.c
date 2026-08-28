#include "gd32f30x.h"

#include "boot_config.h"
#include "boot_image.h"
#include "boot_jump.h"
#include "boot_metadata.h"
#include "boot_protocol.h"
#include "boot_request.h"
#include "boot_time.h"
#include "boot_uart.h"
#include "boot_watchdog.h"

volatile uint8_t g_boot_stage = 0U;
volatile uint8_t g_boot_forced_mode = 0U;
volatile uint8_t g_boot_pending_update_found = 0U;
volatile uint8_t g_boot_staging_valid = 0U;
volatile uint8_t g_boot_application_valid = 0U;

static uint8_t boot_pending_update_process(void)
{
	boot_metadata_record_t metadata;
	boot_image_result_t install_result;

	if (boot_metadata_read(&metadata) == 0U)
	{
		return 1U;
	}

	g_boot_pending_update_found = 1U;
	g_boot_staging_valid = boot_image_staging_is_valid(&metadata);

	if ((boot_metadata_is_installed(&metadata) != 0U) && (boot_image_active_is_valid(&metadata) != 0U))
	{
		return 1U;
	}

	if (g_boot_staging_valid != 0U)
	{
		g_boot_stage = 4U;
		install_result = boot_image_install_pending(&metadata);

		if (install_result == BOOT_IMAGE_RESULT_OK)
		{
			boot_metadata_init();

			if (boot_metadata_read(&metadata) != 0U)
			{
				return boot_image_active_is_valid(&metadata);
			}
		}

		return 0U;
	}

	if ((boot_metadata_is_copy_started(&metadata) == 0U) && (boot_image_active_is_valid((const boot_metadata_record_t *)0) != 0U))
	{
		(void)boot_metadata_clear();
		g_boot_pending_update_found = 0U;
		return 1U;
	}

	return 0U;
}

int main(void)
{
	boot_protocol_result_t protocol_result;
	uint8_t pending_result;

	g_boot_stage = 1U;
	boot_time_init();

	if (g_boot_time_initialized == 0U)
	{
		while (1)
		{
		}
	}

	g_boot_stage = 2U;
	boot_watchdog_init();
	boot_uart_init();

	g_boot_stage = 3U;
	g_boot_forced_mode = boot_request_take();
	boot_metadata_init();

	pending_result = boot_pending_update_process();

	if (pending_result == 0U)
	{
		g_boot_forced_mode = 1U;
	}

	if (g_boot_metadata_installed != 0U)
	{
		boot_metadata_record_t metadata;

		if (boot_metadata_read(&metadata) != 0U)
		{
			g_boot_application_valid = boot_image_active_is_valid(&metadata);
		}
	}
	else
	{
		g_boot_application_valid = boot_image_current_active_is_valid();
	}

	if (g_boot_application_valid == 0U)
	{
		g_boot_forced_mode = 1U;
	}

	g_boot_stage = 5U;

#if (BOOT_ALLOW_UART_ENTRY_ON_NORMAL_BOOT == 0U)
	if ((g_boot_forced_mode == 0U) && (g_boot_application_valid != 0U))
	{
		boot_jump_to_application();
	}
#endif

	protocol_result = boot_protocol_run(g_boot_forced_mode, g_boot_application_valid);

	if ((protocol_result == BOOT_PROTOCOL_RUN_APPLICATION) && (boot_image_current_active_is_valid() != 0U))
	{
		g_boot_stage = 6U;
		boot_jump_to_application();
	}

	g_boot_stage = 0xEEU;

	while (1)
	{
		boot_watchdog_service();
	}
}
