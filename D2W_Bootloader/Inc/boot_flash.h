#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include <stdint.h>

typedef enum
{
	BOOT_FLASH_RESULT_OK = 0,
	BOOT_FLASH_RESULT_INVALID_ARGUMENT,
	BOOT_FLASH_RESULT_INVALID_RANGE,
	BOOT_FLASH_RESULT_NOT_ERASED,
	BOOT_FLASH_RESULT_ERASE_FAILED,
	BOOT_FLASH_RESULT_PROGRAM_FAILED,
	BOOT_FLASH_RESULT_VERIFY_FAILED
} boot_flash_result_t;

extern volatile boot_flash_result_t g_boot_flash_last_result;
extern volatile uint32_t g_boot_flash_last_address;
extern volatile uint32_t g_boot_flash_erase_count;
extern volatile uint32_t g_boot_flash_program_word_count;

boot_flash_result_t boot_flash_erase_staging(uint32_t image_size);
boot_flash_result_t boot_flash_erase_application(uint32_t image_size);
boot_flash_result_t boot_flash_erase_metadata(void);
boot_flash_result_t boot_flash_program(uint32_t address, const uint8_t *data, uint32_t length);
uint8_t boot_flash_range_is_erased(uint32_t address, uint32_t length);
uint32_t boot_flash_crc32(uint32_t address, uint32_t length);

#endif 
