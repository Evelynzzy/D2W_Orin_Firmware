#ifndef BOOT_METADATA_H
#define BOOT_METADATA_H

#include <stdint.h>

#define BOOT_METADATA_MAGIC   (0x44575550UL)
#define BOOT_METADATA_FORMAT_VERSION   (1U)
#define BOOT_METADATA_READY_COMMIT   (0xA55A3CC3UL)
#define BOOT_METADATA_COPY_STARTED_COMMIT   (0x5AA5C33CUL)
#define BOOT_METADATA_INSTALLED_COMMIT   (0x3CC35AA5UL)

typedef struct
{
	uint32_t magic;
	uint16_t format_version;
	uint16_t record_size;
	uint32_t sequence;
	uint32_t image_size;
	uint32_t image_crc32;
	uint32_t image_version;
	uint32_t board_id;
	uint32_t header_crc32;
	uint32_t ready_commit;
	uint32_t copy_started_commit;
	uint32_t installed_commit;
	uint32_t reserved[5];
} boot_metadata_record_t;

extern volatile uint8_t g_boot_metadata_valid;
extern volatile uint8_t g_boot_metadata_copy_started;
extern volatile uint8_t g_boot_metadata_installed;
extern volatile boot_metadata_record_t g_boot_metadata_record;

void boot_metadata_init(void);
uint8_t boot_metadata_read(boot_metadata_record_t *record);
uint8_t boot_metadata_is_copy_started(const boot_metadata_record_t *record);
uint8_t boot_metadata_is_installed(const boot_metadata_record_t *record);
uint8_t boot_metadata_write_ready(uint32_t image_size, uint32_t image_crc32, uint32_t image_version);
uint8_t boot_metadata_mark_copy_started(void);
uint8_t boot_metadata_mark_installed(void);
uint8_t boot_metadata_clear(void);

#endif 
