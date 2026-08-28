#ifndef BOOT_UART_H
#define BOOT_UART_H

#include <stdint.h>

typedef enum
{
	BOOT_UART_RESULT_OK = 0,
	BOOT_UART_RESULT_TIMEOUT,
	BOOT_UART_RESULT_ERROR
} boot_uart_result_t;

extern volatile uint8_t g_boot_uart_initialized;
extern volatile uint32_t g_boot_uart_rx_count;
extern volatile uint32_t g_boot_uart_tx_count;
extern volatile uint32_t g_boot_uart_error_count;

void boot_uart_init(void);
void boot_uart_deinit(void);
void boot_uart_flush_rx(void);
void boot_uart_send_byte(uint8_t data);
void boot_uart_send(const uint8_t *data, uint32_t length);
void boot_uart_wait_transmission_complete(void);

boot_uart_result_t boot_uart_receive_byte(uint8_t *data, uint32_t timeout_ms);

#endif 