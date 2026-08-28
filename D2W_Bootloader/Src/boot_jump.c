#include "boot_jump.h"

#include "boot_config.h"
#include "boot_uart.h"


__attribute__((naked, noreturn))
static void boot_jump_branch(uint32_t initial_msp, uint32_t reset_handler)
{
	__asm volatile(
			"movs r2, #0       \n"
			"msr basepri, r2   \n"
			"msr faultmask, r2 \n"
			"msr control, r2   \n"
			"msr psp, r2       \n"
			"isb               \n"
			"msr msp, r0       \n"
			"dsb               \n"
			"isb               \n"
			"cpsie i           \n"
			"bx r1             \n");
}

void boot_jump_to_application(void)
{
	uint32_t index;
	uint32_t initial_msp = *(const volatile uint32_t *)BOOT_APPLICATION_ADDRESS;
	uint32_t reset_handler = *(const volatile uint32_t *)(BOOT_APPLICATION_ADDRESS + 4UL);

	__disable_irq();

	SysTick->CTRL = 0UL;
	SysTick->LOAD = 0UL;
	SysTick->VAL = 0UL;

	for (index = 0UL; index < 8UL; index++)
	{
		NVIC->ICER[index] = 0xFFFFFFFFUL;
		NVIC->ICPR[index] = 0xFFFFFFFFUL;
	}

	SCB->ICSR = (1UL << 25) | (1UL << 27);

	boot_uart_deinit();

	SCB->VTOR = BOOT_APPLICATION_ADDRESS;
	__DSB();
	__ISB();

	boot_jump_branch(initial_msp, reset_handler);
}