#include "boot_metadata.h"

#include <stddef.h>

#include "boot_config.h"
#include "boot_crc32.h"
#include "boot_flash.h"

typedef char boot_metadata_record_must_be_64_bytes[(sizeof(boot_metadata_record_t) == 64U) ? 1 : -1];

volatile uint8_t g_boot_metadata_valid = 0U;
volatile uint8_t g_boot_metadata_copy_started = 0U;
volatile uint8_t g_boot_metadata_installed = 0U;
volatile boot_metadata_record_t g_boot_metadata_record;

static void boot_metadata_public_clear(void)
{
	volatile uint32_t *destination = (volatile uint32_t *)&g_boot_metadata_record;
	uint32_t index;

	for (index = 0UL; index < (sizeof(boot_metadata_record_t) / sizeof(uint32_t)); index++)
	{
		destination[index] = 0UL;
	}
}

static void boot_metadata_public_copy(const boot_metadata_record_t *source)
{
	volatile uint32_t *destination = (volatile uint32_t *)&g_boot_metadata_record;
	const uint32_t *source_words = (const uint32_t *)source;
	uint32_t index;

	for (index = 0UL; index < (sizeof(boot_metadata_record_t) / sizeof(uint32_t)); index++)
	{
		destination[index] = source_words[index];
	}
}

static uint8_t boot_metadata_header_is_valid(const boot_metadata_record_t *record)
{
	uint32_t calculated_crc;

	if (record->magic != BOOT_METADATA_MAGIC)
	{
		return 0U;
	}

	if (record->format_version != BOOT_METADATA_FORMAT_VERSION)
	{
		return 0U;
	}

	if (record->record_size != sizeof(boot_metadata_record_t))
	{
		return 0U;
	}

	if ((record->image_size < 8UL) || (record->image_size > BOOT_APPLICATION_MAX_SIZE))
	{
		return 0U;
	}

	if (record->board_id != BOOT_BOARD_ID)
	{
		return 0U;
	}

	calculated_crc = boot_crc32_calculate((const uint8_t *)record, (uint32_t)offsetof(boot_metadata_record_t, header_crc32));

	if (calculated_crc != record->header_crc32)
	{
		return 0U;
	}

	if (record->ready_commit != BOOT_METADATA_READY_COMMIT)
	{
		return 0U;
	}

	return 1U;
}

void boot_metadata_init(void)
{
	boot_metadata_record_t record;

	boot_metadata_public_clear();
	g_boot_metadata_valid = 0U;
	g_boot_metadata_copy_started = 0U;
	g_boot_metadata_installed = 0U;

	if (boot_metadata_read(&record) != 0U)
	{
		boot_metadata_public_copy(&record);
		g_boot_metadata_valid = 1U;
		g_boot_metadata_copy_started = boot_metadata_is_copy_started(&record);
		g_boot_metadata_installed = boot_metadata_is_installed(&record);
	}
}

uint8_t boot_metadata_read(boot_metadata_record_t *record)
{
	const volatile uint32_t *source = (const volatile uint32_t *)BOOT_METADATA_ADDRESS;
	uint32_t *destination;
	uint32_t index;

	if (record == (boot_metadata_record_t *)0)
	{
		return 0U;
	}

	destination = (uint32_t *)record;

	for (index = 0UL; index < (sizeof(boot_metadata_record_t) / sizeof(uint32_t)); index++)
	{
		destination[index] = source[index];
	}

	return boot_metadata_header_is_valid(record);
}

uint8_t boot_metadata_is_copy_started(const boot_metadata_record_t *record)
{
	if (record == (const boot_metadata_record_t *)0)
	{
		return 0U;
	}

	return (record->copy_started_commit == BOOT_METADATA_COPY_STARTED_COMMIT) ? 1U : 0U;
}

uint8_t boot_metadata_is_installed(const boot_metadata_record_t *record)
{
	if (record == (const boot_metadata_record_t *)0)
	{
		return 0U;
	}

	return (record->installed_commit == BOOT_METADATA_INSTALLED_COMMIT) ? 1U : 0U;
}

uint8_t boot_metadata_write_ready(uint32_t image_size, uint32_t image_crc32, uint32_t image_version)
{
	boot_metadata_record_t record;
	uint32_t *words = (uint32_t *)&record;
	uint32_t index;
	uint32_t ready_commit;
	boot_flash_result_t flash_result;

	if ((image_size < 8UL) || (image_size > BOOT_APPLICATION_MAX_SIZE))
	{
		return 0U;
	}

	for (index = 0UL; index < (sizeof(boot_metadata_record_t) / sizeof(uint32_t)); index++)
	{
		words[index] = 0xFFFFFFFFUL;
	}

	record.magic = BOOT_METADATA_MAGIC;
	record.format_version = BOOT_METADATA_FORMAT_VERSION;
	record.record_size = (uint16_t)sizeof(boot_metadata_record_t);
	record.sequence = image_version;
	record.image_size = image_size;
	record.image_crc32 = image_crc32;
	record.image_version = image_version;
	record.board_id = BOOT_BOARD_ID;
	record.header_crc32 = boot_crc32_calculate((const uint8_t *)&record, (uint32_t)offsetof(boot_metadata_record_t, header_crc32));

	flash_result = boot_flash_program(BOOT_METADATA_ADDRESS, (const uint8_t *)&record, (uint32_t)offsetof(boot_metadata_record_t, ready_commit));

	if (flash_result != BOOT_FLASH_RESULT_OK)
	{
		return 0U;
	}

	ready_commit = BOOT_METADATA_READY_COMMIT;
	flash_result = boot_flash_program(BOOT_METADATA_ADDRESS + (uint32_t)offsetof(boot_metadata_record_t, ready_commit), (const uint8_t *)&ready_commit, sizeof(ready_commit));

	if (flash_result != BOOT_FLASH_RESULT_OK)
	{
		return 0U;
	}

	boot_metadata_init();
	return g_boot_metadata_valid;
}

uint8_t boot_metadata_mark_copy_started(void)
{
	uint32_t commit = BOOT_METADATA_COPY_STARTED_COMMIT;
	boot_flash_result_t result;

	if (g_boot_metadata_valid == 0U)
	{
		return 0U;
	}

	if (g_boot_metadata_copy_started != 0U)
	{
		return 1U;
	}

	result = boot_flash_program(BOOT_METADATA_ADDRESS + (uint32_t)offsetof(boot_metadata_record_t, copy_started_commit), (const uint8_t *)&commit, sizeof(commit));

	if (result != BOOT_FLASH_RESULT_OK)
	{
		return 0U;
	}

	boot_metadata_init();
	return g_boot_metadata_copy_started;
}

uint8_t boot_metadata_mark_installed(void)
{
	uint32_t commit = BOOT_METADATA_INSTALLED_COMMIT;
	boot_flash_result_t result;

	if ((g_boot_metadata_valid == 0U) || (g_boot_metadata_copy_started == 0U))
	{
		return 0U;
	}

	if (g_boot_metadata_installed != 0U)
	{
		return 1U;
	}

	result = boot_flash_program(BOOT_METADATA_ADDRESS + (uint32_t)offsetof(boot_metadata_record_t, installed_commit), (const uint8_t *)&commit, sizeof(commit));

	if (result != BOOT_FLASH_RESULT_OK)
	{
		return 0U;
	}

	boot_metadata_init();
	return g_boot_metadata_installed;
}

uint8_t boot_metadata_clear(void)
{
	if (boot_flash_erase_metadata() != BOOT_FLASH_RESULT_OK)
	{
		return 0U;
	}

	boot_metadata_init();
	return (g_boot_metadata_valid == 0U) ? 1U : 0U;
}
