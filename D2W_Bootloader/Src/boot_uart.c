#include "boot_uart.h"

#include "boot_config.h"
#include "boot_time.h"
#include "boot_watchdog.h"

volatile uint8_t g_boot_uart_initialized = 0U;
volatile uint32_t g_boot_uart_rx_count = 0UL;
volatile uint32_t g_boot_uart_tx_count = 0UL;
volatile uint32_t g_boot_uart_error_count = 0UL;

static uint8_t boot_uart_error_is_active(void)
{
	if (usart_flag_get(BOOT_UART_PERIPH, USART_FLAG_ORERR) != RESET)
	{
		return 1U;
	}

	if (usart_flag_get(BOOT_UART_PERIPH, USART_FLAG_NERR) != RESET)
	{
		return 1U;
	}

	if (usart_flag_get(BOOT_UART_PERIPH, USART_FLAG_FERR) != RESET)
	{
		return 1U;
	}

	if (usart_flag_get(BOOT_UART_PERIPH, USART_FLAG_PERR) != RESET)
	{
		return 1U;
	}

	return 0U;
}

static void boot_uart_error_clear(void)
{
	volatile uint16_t discarded_data;
	discarded_data = usart_data_receive(BOOT_UART_PERIPH);
	(void)discarded_data;
}

void boot_uart_init(void)
{
	rcu_periph_clock_enable(RCU_AF);
	rcu_periph_clock_enable(BOOT_UART_GPIO_RCU);
	rcu_periph_clock_enable(BOOT_UART_RCU);

	gpio_init(BOOT_UART_TX_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, BOOT_UART_TX_PIN);
	gpio_init(BOOT_UART_RX_PORT, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, BOOT_UART_RX_PIN);

	usart_deinit(BOOT_UART_PERIPH);
	usart_baudrate_set(BOOT_UART_PERIPH, BOOT_UART_BAUD_RATE);
	usart_word_length_set(BOOT_UART_PERIPH, USART_WL_8BIT);
	usart_stop_bit_set(BOOT_UART_PERIPH, USART_STB_1BIT);
	usart_parity_config(BOOT_UART_PERIPH, USART_PM_NONE);
	usart_hardware_flow_rts_config(BOOT_UART_PERIPH, USART_RTS_DISABLE);
	usart_hardware_flow_cts_config(BOOT_UART_PERIPH, USART_CTS_DISABLE);
	usart_transmit_config(BOOT_UART_PERIPH, USART_TRANSMIT_ENABLE);
	usart_receive_config(BOOT_UART_PERIPH, USART_RECEIVE_ENABLE);
	usart_enable(BOOT_UART_PERIPH);

	g_boot_uart_rx_count = 0UL;
	g_boot_uart_tx_count = 0UL;
	g_boot_uart_error_count = 0UL;
	g_boot_uart_initialized = 1U;

	boot_uart_flush_rx();
}

void boot_uart_deinit(void)
{
	usart_disable(BOOT_UART_PERIPH);
	g_boot_uart_initialized = 0U;
}

void boot_uart_flush_rx(void)
{
	while (usart_flag_get(BOOT_UART_PERIPH, USART_FLAG_RBNE) != RESET)
	{
		(void)usart_data_receive(BOOT_UART_PERIPH);
	}

	if (boot_uart_error_is_active() != 0U)
	{
		boot_uart_error_clear();
	}
}

void boot_uart_send_byte(uint8_t data)
{
	while (usart_flag_get(BOOT_UART_PERIPH, USART_FLAG_TBE) == RESET)
	{
			boot_watchdog_service();
	}

	usart_data_transmit(BOOT_UART_PERIPH, (uint16_t)data);

	if (g_boot_uart_tx_count != 0xFFFFFFFFUL)
	{
		g_boot_uart_tx_count++;
	}
}

void boot_uart_send(const uint8_t *data, uint32_t length)
{
	uint32_t index;

	if (data == (const uint8_t *)0)
	{
		return;
	}

	for (index = 0UL; index < length; index++)
	{
		boot_uart_send_byte(data[index]);
	}
}

void boot_uart_wait_transmission_complete(void)
{
	while (usart_flag_get(BOOT_UART_PERIPH, USART_FLAG_TC) == RESET)
	{
		boot_watchdog_service();
	}
}

boot_uart_result_t boot_uart_receive_byte(uint8_t *data, uint32_t timeout_ms)
{
	uint32_t start_ms;

	if ((data == (uint8_t *)0) || (g_boot_uart_initialized == 0U))
	{
		return BOOT_UART_RESULT_ERROR;
	}

	start_ms = boot_time_now();

	while (1)
	{
		if (boot_uart_error_is_active() != 0U)
		{
			boot_uart_error_clear();

			if (g_boot_uart_error_count != 0xFFFFFFFFUL)
			{
				g_boot_uart_error_count++;
			}

			return BOOT_UART_RESULT_ERROR;
		}

		if (usart_flag_get(BOOT_UART_PERIPH, USART_FLAG_RBNE) != RESET)
		{
			*data = (uint8_t) usart_data_receive(BOOT_UART_PERIPH);

			if (g_boot_uart_rx_count != 0xFFFFFFFFUL)
			{
				g_boot_uart_rx_count++;
			}

			return BOOT_UART_RESULT_OK;
		}

		boot_watchdog_service();

		if (boot_time_elapsed(start_ms, timeout_ms) != 0U)
		{
			return BOOT_UART_RESULT_TIMEOUT;
		}
	}
}