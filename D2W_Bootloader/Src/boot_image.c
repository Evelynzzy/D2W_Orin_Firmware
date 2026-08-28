#include "boot_image.h"

#include "boot_config.h"
#include "boot_flash.h"
#include "boot_time.h"
#include "boot_watchdog.h"

#define BOOT_IMAGE_COPY_BLOCK_SIZE  (256UL)

volatile boot_image_result_t g_boot_image_last_result = BOOT_IMAGE_RESULT_OK;
volatile uint8_t g_boot_active_application_valid = 0U;
volatile uint32_t g_boot_image_copy_offset = 0UL;
volatile uint32_t g_boot_image_install_count = 0UL;

static void boot_image_test_copy_delay(void)
{
#if (BOOT_TEST_COPY_BLOCK_DELAY_MS > 0UL)
	uint32_t start_ms = boot_time_now();

	while (boot_time_elapsed(start_ms, BOOT_TEST_COPY_BLOCK_DELAY_MS) == 0U)
	{
		boot_watchdog_service();
	}
#endif
}

uint8_t boot_image_vector_is_valid(uint32_t storage_address, uint32_t execution_address, uint32_t image_size)
{
	uint32_t initial_msp;
	uint32_t reset_handler;
	uint32_t reset_address;
	uint32_t execution_end;

	if ((image_size < 8UL) || (image_size > BOOT_APPLICATION_MAX_SIZE))
	{
		return 0U;
	}

	initial_msp = *(const volatile uint32_t *)storage_address;
	reset_handler = *(const volatile uint32_t *)(storage_address + 4UL);
	reset_address = reset_handler & ~1UL;
	execution_end = execution_address + image_size;

	if ((initial_msp < BOOT_SRAM_START_ADDRESS) || (initial_msp > BOOT_SRAM_END_EXCLUSIVE) || ((initial_msp & 7UL) != 0UL))
	{
		return 0U;
	}

	if ((reset_handler & 1UL) == 0UL)
	{
		return 0U;
	}

	if ((reset_address < execution_address) || (reset_address >= execution_end))
	{
		return 0U;
	}

	return 1U;
}

uint8_t boot_image_active_is_valid(const boot_metadata_record_t *metadata)
{
	uint32_t image_size = BOOT_APPLICATION_MAX_SIZE;

	if (metadata != (const boot_metadata_record_t *)0)
	{
		image_size = metadata->image_size;
	}

	if (boot_image_vector_is_valid(BOOT_APPLICATION_ADDRESS, BOOT_APPLICATION_ADDRESS, image_size) == 0U)
	{
		g_boot_active_application_valid = 0U;
		return 0U;
	}

	if ((metadata != (const boot_metadata_record_t *)0) && (boot_metadata_is_installed(metadata) != 0U))
	{
		if (boot_flash_crc32(BOOT_APPLICATION_ADDRESS, metadata->image_size) != metadata->image_crc32)
		{
			g_boot_active_application_valid = 0U;
			return 0U;
		}
	}

	g_boot_active_application_valid = 1U;
	return 1U;
}

uint8_t boot_image_current_active_is_valid(void)
{
	boot_metadata_record_t metadata;

	if ((boot_metadata_read(&metadata) != 0U) && (boot_metadata_is_installed(&metadata) != 0U))
	{
		return boot_image_active_is_valid(&metadata);
	}

	return boot_image_active_is_valid((const boot_metadata_record_t *)0);
}

uint8_t boot_image_staging_is_valid(const boot_metadata_record_t *metadata)
{
	if (metadata == (const boot_metadata_record_t *)0)
	{
		return 0U;
	}

	if (boot_image_vector_is_valid(BOOT_STAGING_ADDRESS, BOOT_APPLICATION_ADDRESS, metadata->image_size) == 0U)
	{
		return 0U;
	}

	if (boot_flash_crc32(BOOT_STAGING_ADDRESS, metadata->image_size) != metadata->image_crc32)
	{
		return 0U;
	}

	return 1U;
}

boot_image_result_t boot_image_install_pending(const boot_metadata_record_t *metadata)
{
	const uint8_t *source;
	uint32_t block_length;
	boot_flash_result_t flash_result;

	if (metadata == (const boot_metadata_record_t *)0)
	{
		g_boot_image_last_result = BOOT_IMAGE_RESULT_NO_METADATA;
		return BOOT_IMAGE_RESULT_NO_METADATA;
	}

	if (boot_image_staging_is_valid(metadata) == 0U)
	{
		g_boot_image_last_result = BOOT_IMAGE_RESULT_STAGING_CRC_FAILED;
		return BOOT_IMAGE_RESULT_STAGING_CRC_FAILED;
	}

	if (boot_metadata_mark_copy_started() == 0U)
	{
		g_boot_image_last_result = BOOT_IMAGE_RESULT_METADATA_FAILED;
		return BOOT_IMAGE_RESULT_METADATA_FAILED;
	}

	flash_result = boot_flash_erase_application(metadata->image_size);

	if (flash_result != BOOT_FLASH_RESULT_OK)
	{
		g_boot_image_last_result = BOOT_IMAGE_RESULT_ERASE_FAILED;
		return BOOT_IMAGE_RESULT_ERASE_FAILED;
	}

	g_boot_image_copy_offset = 0UL;

	while (g_boot_image_copy_offset < metadata->image_size)
	{
		block_length = metadata->image_size - g_boot_image_copy_offset;

		if (block_length > BOOT_IMAGE_COPY_BLOCK_SIZE)
		{
			block_length = BOOT_IMAGE_COPY_BLOCK_SIZE;
		}

		source = (const uint8_t *)(BOOT_STAGING_ADDRESS + g_boot_image_copy_offset);
		flash_result = boot_flash_program(BOOT_APPLICATION_ADDRESS + g_boot_image_copy_offset, source, block_length);

		if (flash_result != BOOT_FLASH_RESULT_OK)
		{
			g_boot_image_last_result = BOOT_IMAGE_RESULT_PROGRAM_FAILED;
			return BOOT_IMAGE_RESULT_PROGRAM_FAILED;
		}

		g_boot_image_copy_offset += block_length;
		boot_watchdog_feed_now();
		boot_image_test_copy_delay();
	}

	if (boot_flash_crc32(BOOT_APPLICATION_ADDRESS, metadata->image_size) != metadata->image_crc32)
	{
		g_boot_image_last_result = BOOT_IMAGE_RESULT_ACTIVE_CRC_FAILED;
		return BOOT_IMAGE_RESULT_ACTIVE_CRC_FAILED;
	}

	if (boot_image_vector_is_valid(BOOT_APPLICATION_ADDRESS, BOOT_APPLICATION_ADDRESS, metadata->image_size) == 0U)
	{
		g_boot_image_last_result = BOOT_IMAGE_RESULT_INVALID_VECTOR;
		return BOOT_IMAGE_RESULT_INVALID_VECTOR;
	}

	if (boot_metadata_mark_installed() == 0U)
	{
		g_boot_image_last_result = BOOT_IMAGE_RESULT_METADATA_FAILED;
		return BOOT_IMAGE_RESULT_METADATA_FAILED;
	}

	if (g_boot_image_install_count != 0xFFFFFFFFUL)
	{
		g_boot_image_install_count++;
	}

	g_boot_active_application_valid = 1U;
	g_boot_image_last_result = BOOT_IMAGE_RESULT_OK;
	return BOOT_IMAGE_RESULT_OK;
}
