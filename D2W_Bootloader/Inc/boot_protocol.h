#ifndef BOOT_PROTOCOL_H
#define BOOT_PROTOCOL_H

#include <stdint.h>

typedef enum
{
	BOOT_PROTOCOL_RUN_APPLICATION = 0,
	BOOT_PROTOCOL_STAY_IN_BOOTLOADER
} boot_protocol_result_t;

typedef enum
{
	BOOT_PROTOCOL_STATE_IDLE = 0,
	BOOT_PROTOCOL_STATE_RECEIVING,
	BOOT_PROTOCOL_STATE_READY,
	BOOT_PROTOCOL_STATE_ERROR
} boot_protocol_state_t;

extern volatile boot_protocol_state_t g_boot_protocol_state;
extern volatile uint32_t g_boot_protocol_next_offset;
extern volatile uint32_t g_boot_protocol_expected_size;
extern volatile uint32_t g_boot_protocol_expected_crc32;
extern volatile uint32_t g_boot_protocol_image_version;
extern volatile uint32_t g_boot_protocol_valid_packet_count;
extern volatile uint32_t g_boot_protocol_error_count;

boot_protocol_result_t boot_protocol_run(uint8_t forced_mode, uint8_t application_valid);

#endif 
