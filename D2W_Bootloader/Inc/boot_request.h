#ifndef BOOT_REQUEST_H
#define BOOT_REQUEST_H

#include <stdint.h>

extern volatile uint8_t g_boot_update_requested;

uint8_t boot_request_take(void);
void boot_request_clear(void);

#endif 
