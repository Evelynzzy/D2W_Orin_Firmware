#ifndef BOOT_TIME_H
#define BOOT_TIME_H

#include <stdint.h>

extern volatile uint32_t g_boot_time_ms;
extern volatile uint8_t g_boot_time_initialized;

void boot_time_init(void);
uint32_t boot_time_now(void);
uint8_t boot_time_elapsed(uint32_t start_ms, uint32_t interval_ms);
void boot_time_delay_ms(uint32_t delay_ms);

#endif 
