#ifndef BOOT_IMAGE_H
#define BOOT_IMAGE_H

#include <stdint.h>

#include "boot_metadata.h"

typedef enum
{
	BOOT_IMAGE_RESULT_OK = 0,
	BOOT_IMAGE_RESULT_NO_METADATA,
	BOOT_IMAGE_RESULT_INVALID_VECTOR,
	BOOT_IMAGE_RESULT_STAGING_CRC_FAILED,
	BOOT_IMAGE_RESULT_ACTIVE_CRC_FAILED,
	BOOT_IMAGE_RESULT_METADATA_FAILED,
	BOOT_IMAGE_RESULT_ERASE_FAILED,
	BOOT_IMAGE_RESULT_PROGRAM_FAILED
} boot_image_result_t;

extern volatile boot_image_result_t g_boot_image_last_result;
extern volatile uint8_t g_boot_active_application_valid;
extern volatile uint32_t g_boot_image_copy_offset;
extern volatile uint32_t g_boot_image_install_count;

uint8_t boot_image_vector_is_valid(uint32_t storage_address, uint32_t execution_address, uint32_t image_size);
uint8_t boot_image_active_is_valid(const boot_metadata_record_t *metadata);
uint8_t boot_image_current_active_is_valid(void);
uint8_t boot_image_staging_is_valid(const boot_metadata_record_t *metadata);
boot_image_result_t boot_image_install_pending(const boot_metadata_record_t *metadata);

#endif 
