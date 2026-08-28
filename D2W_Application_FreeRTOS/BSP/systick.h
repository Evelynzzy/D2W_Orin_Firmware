#ifndef SYSTICK_H
#define SYSTICK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void systick_config(void);
uint32_t systick_get_ms(void);
uint32_t systick_get_ms_from_isr(void);
void delay_1ms(uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
