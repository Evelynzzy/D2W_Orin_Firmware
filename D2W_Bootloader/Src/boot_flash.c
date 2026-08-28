#include "boot_flash.h"

#include "boot_config.h"
#include "boot_crc32.h"
#include "boot_watchdog.h"

volatile boot_flash_result_t g_boot_flash_last_result = BOOT_FLASH_RESULT_OK;
volatile uint32_t g_boot_flash_last_address = 0UL;
volatile uint32_t g_boot_flash_erase_count = 0UL;
volatile uint32_t g_boot_flash_program_word_count = 0UL;

static uint8_t boot_flash_range_is_inside(uint32_t address, uint32_t length, uint32_t region_start, uint32_t region_end)
{
	uint32_t range_end;

	if (length == 0UL)
	{
		return 0U;
	}

	if (address < region_start)
	{
		return 0U;
	}

	range_end = address + length;

	if ((range_end < address) || (range_end > region_end))
	{
		return 0U;
	}

	return 1U;
}

static uint8_t boot_flash_write_range_is_allowed(uint32_t address, uint32_t length)
{
	if (boot_flash_range_is_inside(address, length, BOOT_APPLICATION_ADDRESS, BOOT_APPLICATION_END_EXCLUSIVE) != 0U)
	{
		return 1U;
	}

	if (boot_flash_range_is_inside(address, length, BOOT_STAGING_ADDRESS, BOOT_STAGING_END_EXCLUSIVE) != 0U)
	{
		return 1U;
	}

	if (boot_flash_range_is_inside(address, length, BOOT_METADATA_ADDRESS, BOOT_METADATA_END_EXCLUSIVE) != 0U)
	{
		return 1U;
	}

	return 0U;
}

static void boot_flash_flags_clear(void)
{
	fmc_flag_clear(FMC_FLAG_BANK0_END);
	fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
	fmc_flag_clear(FMC_FLAG_BANK0_PGERR);
}

static boot_flash_result_t boot_flash_erase_range(uint32_t address, uint32_t length, uint32_t region_start, uint32_t region_end)
{
	uint32_t page_address;
	uint32_t erase_end;
	uint32_t primask;
	fmc_state_enum fmc_state;

	if ((address % BOOT_FLASH_PAGE_SIZE) != 0UL)
	{
		return BOOT_FLASH_RESULT_INVALID_ARGUMENT;
	}

	if (boot_flash_range_is_inside(address, length, region_start, region_end) == 0U)
	{
		return BOOT_FLASH_RESULT_INVALID_RANGE;
	}

	erase_end = address + ((length + BOOT_FLASH_PAGE_SIZE - 1UL) & ~(BOOT_FLASH_PAGE_SIZE - 1UL));

	fmc_unlock();

	for (page_address = address; page_address < erase_end; page_address += BOOT_FLASH_PAGE_SIZE)
	{
		g_boot_flash_last_address = page_address;
		boot_watchdog_feed_now();
		boot_flash_flags_clear();

		primask = __get_PRIMASK();
		__disable_irq();
		fmc_state = fmc_page_erase(page_address);

		if (primask == 0UL)
		{
			__enable_irq();
		}

		boot_flash_flags_clear();
		boot_watchdog_feed_now();

		if (fmc_state != FMC_READY)
		{
			fmc_lock();
			g_boot_flash_last_result = BOOT_FLASH_RESULT_ERASE_FAILED;
			return BOOT_FLASH_RESULT_ERASE_FAILED;
		}

		if (g_boot_flash_erase_count != 0xFFFFFFFFUL)
		{
			g_boot_flash_erase_count++;
		}
	}

	fmc_lock();
	g_boot_flash_last_result = BOOT_FLASH_RESULT_OK;
	return BOOT_FLASH_RESULT_OK;
}

boot_flash_result_t boot_flash_erase_staging(uint32_t image_size)
{
	return boot_flash_erase_range(BOOT_STAGING_ADDRESS, image_size, BOOT_STAGING_ADDRESS, BOOT_STAGING_END_EXCLUSIVE);
}

boot_flash_result_t boot_flash_erase_application(uint32_t image_size)
{
	return boot_flash_erase_range(BOOT_APPLICATION_ADDRESS, image_size, BOOT_APPLICATION_ADDRESS, BOOT_APPLICATION_END_EXCLUSIVE);
}

boot_flash_result_t boot_flash_erase_metadata(void)
{
	return boot_flash_erase_range(BOOT_METADATA_ADDRESS, BOOT_METADATA_SIZE, BOOT_METADATA_ADDRESS, BOOT_METADATA_END_EXCLUSIVE);
}

uint8_t boot_flash_range_is_erased(uint32_t address, uint32_t length)
{
	const volatile uint8_t *memory;
	uint32_t index;

	if (boot_flash_write_range_is_allowed(address, length) == 0U)
	{
		return 0U;
	}

	memory = (const volatile uint8_t *)address;

	for (index = 0UL; index < length; index++)
	{
		if (memory[index] != 0xFFU)
		{
			return 0U;
		}
	}

	return 1U;
}

static boot_flash_result_t boot_flash_program_word_unlocked(uint32_t address, uint32_t data)
{
	uint32_t primask;
	fmc_state_enum fmc_state;

	if ((address & 3UL) != 0UL)
	{
		return BOOT_FLASH_RESULT_INVALID_ARGUMENT;
	}

	if (boot_flash_write_range_is_allowed(address, 4UL) == 0U)
	{
		return BOOT_FLASH_RESULT_INVALID_RANGE;
	}

	if (*(const volatile uint32_t *)address != 0xFFFFFFFFUL)
	{
		return BOOT_FLASH_RESULT_NOT_ERASED;
	}

	g_boot_flash_last_address = address;
	boot_watchdog_service();
	boot_flash_flags_clear();

	primask = __get_PRIMASK();
	__disable_irq();
	fmc_state = fmc_word_program(address, data);

	if (primask == 0UL)
	{
		__enable_irq();
	}

	boot_flash_flags_clear();

	if (fmc_state != FMC_READY)
	{
		return BOOT_FLASH_RESULT_PROGRAM_FAILED;
	}

	if (*(const volatile uint32_t *)address != data)
	{
		return BOOT_FLASH_RESULT_VERIFY_FAILED;
	}

	if (g_boot_flash_program_word_count != 0xFFFFFFFFUL)
	{
		g_boot_flash_program_word_count++;
	}

	return BOOT_FLASH_RESULT_OK;
}

boot_flash_result_t boot_flash_program(uint32_t address, const uint8_t *data, uint32_t length)
{
	uint32_t index = 0UL;
	uint32_t word;
	uint32_t byte_index;
	uint32_t bytes_in_word;
	boot_flash_result_t result;

	if ((data == (const uint8_t *)0) || (length == 0UL) || ((address & 3UL) != 0UL))
	{
		return BOOT_FLASH_RESULT_INVALID_ARGUMENT;
	}

	if (boot_flash_write_range_is_allowed(address, length) == 0U)
	{
		return BOOT_FLASH_RESULT_INVALID_RANGE;
	}

	fmc_unlock();

	while (index < length)
	{
		word = 0xFFFFFFFFUL;
		bytes_in_word = length - index;

		if (bytes_in_word > 4UL)
		{
			bytes_in_word = 4UL;
		}

		for (byte_index = 0UL; byte_index < bytes_in_word; byte_index++)
		{
			word &= ~(0xFFUL << (byte_index * 8UL));
			word |= ((uint32_t)data[index + byte_index]) << (byte_index * 8UL);
		}

		result = boot_flash_program_word_unlocked(address + index, word);

		if (result != BOOT_FLASH_RESULT_OK)
		{
			fmc_lock();
			g_boot_flash_last_result = result;
			return result;
		}
		index += bytes_in_word;
	}

	fmc_lock();
	g_boot_flash_last_result = BOOT_FLASH_RESULT_OK;
	return BOOT_FLASH_RESULT_OK;
}

uint32_t boot_flash_crc32(uint32_t address, uint32_t length)
{
	return boot_crc32_calculate((const uint8_t *)address, length);
}
