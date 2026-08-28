#include "boot_crc32.h"
#define BOOT_CRC32_POLYNOMIAL    (0xEDB88320UL)

uint32_t boot_crc32_begin(void)
{
	return 0xFFFFFFFFUL;
}

uint32_t boot_crc32_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
	uint32_t index;
	uint32_t bit_index;

	for (index = 0UL; index < length; index++)
	{
		crc ^= (uint32_t)data[index];

		for (bit_index = 0UL; bit_index < 8UL; bit_index++)
		{
			if ((crc & 1UL) != 0UL)
			{
				crc = (crc >> 1UL) ^ BOOT_CRC32_POLYNOMIAL;
			}
			else
			{
				crc >>= 1UL;
			}
		}
	}

	return crc;
}

uint32_t boot_crc32_finish(uint32_t crc)
{
	return ~crc;
}

uint32_t boot_crc32_calculate(const uint8_t *data, uint32_t length)
{
	return boot_crc32_finish(boot_crc32_update(boot_crc32_begin(), data, length));
}
