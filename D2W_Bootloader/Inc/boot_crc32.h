#ifndef BOOT_CRC32_H
#define BOOT_CRC32_H

#include <stdint.h>

uint32_t boot_crc32_begin(void);
uint32_t boot_crc32_update(uint32_t crc, const uint8_t *data, uint32_t length);
uint32_t boot_crc32_finish(uint32_t crc);
uint32_t boot_crc32_calculate(const uint8_t *data, uint32_t length);

#endif 
