#ifndef APP_BOOT_CONTROL_H
#define APP_BOOT_CONTROL_H

#include <stdint.h>

void app_boot_control_vector_init(void);
void app_boot_control_mark_update_request(void);
void app_boot_control_request_update_and_reset(void);

#endif 
