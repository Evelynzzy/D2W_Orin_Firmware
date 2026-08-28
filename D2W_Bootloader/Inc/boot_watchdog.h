#ifndef BOOT_WATCHDOG_H
#define BOOT_WATCHDOG_H

#include <stdint.h>

extern volatile uint8_t g_boot_watchdog_initialized;
extern volatile uint8_t g_boot_watchdog_wdi_level;
extern volatile uint32_t g_boot_watchdog_feed_count;

void boot_watchdog_init(void);
void boot_watchdog_service(void);
void boot_watchdog_feed_now(void);

#endif 
