#ifndef SHUTDOWN_REQUEST_H
#define SHUTDOWN_REQUEST_H

#include <stdint.h>

#include "power_ctrl.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SHUTDOWN_REQUEST_IRQ_PRIORITY
#define SHUTDOWN_REQUEST_IRQ_PRIORITY    5U
#endif

#ifndef SHUTDOWN_REQUEST_SYS_RESET_TIMEOUT_MS
#define SHUTDOWN_REQUEST_SYS_RESET_TIMEOUT_MS    1000U
#endif

#ifndef SHUTDOWN_REQUEST_RESET_STABLE_MS
#define SHUTDOWN_REQUEST_RESET_STABLE_MS    2U
#endif

#ifndef SHUTDOWN_REQUEST_REARM_STABLE_MS
#define SHUTDOWN_REQUEST_REARM_STABLE_MS    5U
#endif

#ifndef SHUTDOWN_REQUEST_DEFINE_IRQ_HANDLER
#define SHUTDOWN_REQUEST_DEFINE_IRQ_HANDLER    0U
#endif

#ifndef SHUTDOWN_REQUEST_FAST_PREPARE_IN_ISR
#define SHUTDOWN_REQUEST_FAST_PREPARE_IN_ISR    0U
#endif

typedef enum
{
	SHUTDOWN_REQUEST_STATE_UNINITIALIZED = 0,
	SHUTDOWN_REQUEST_STATE_ARMED,
	SHUTDOWN_REQUEST_STATE_REQUEST_LATCHED,
	SHUTDOWN_REQUEST_STATE_WAIT_SYS_RESET,
	SHUTDOWN_REQUEST_STATE_COMPLETE,
	SHUTDOWN_REQUEST_STATE_FAULT,
	SHUTDOWN_REQUEST_STATE_FORCED_OFF
} shutdown_request_state_t;

typedef enum
{
	SHUTDOWN_REQUEST_REARM_OK = 0,
	SHUTDOWN_REQUEST_REARM_NOT_INITIALIZED,
	SHUTDOWN_REQUEST_REARM_LINE_NOT_RELEASED,
	SHUTDOWN_REQUEST_REARM_POWER_NOT_ON
} shutdown_request_rearm_result_t;

typedef struct
{
	shutdown_request_state_t state;
	power_ctrl_result_t last_power_result;

	uint8_t initialized;
	uint8_t pin_level;
	uint8_t active;
	uint8_t pending;
	uint8_t latched;
	uint8_t completed;
	uint8_t fault;
	uint8_t forced_off;

	uint32_t event_count;
	uint32_t irq_count;
	uint32_t polling_recovery_count;
	uint32_t reset_wait_ms;
} shutdown_request_status_t;


extern volatile uint8_t shutdown_request_pending;
extern volatile uint8_t shutdown_request_pin_state;
extern volatile uint32_t shutdown_request_event_count;

extern volatile shutdown_request_state_t shutdown_request_state;
extern volatile power_ctrl_result_t shutdown_request_last_power_result;
extern volatile uint8_t shutdown_request_completed;
extern volatile uint8_t shutdown_request_fault;
extern volatile uint8_t shutdown_request_forced_off;
extern volatile uint32_t shutdown_request_irq_count;
extern volatile uint32_t shutdown_request_polling_recovery_count;
extern volatile uint32_t shutdown_request_reset_wait_ms;


void shutdown_request_init(void);
void shutdown_request_task_1ms(void);
void shutdown_request_irq_handler(void);

uint8_t shutdown_request_pending_get(void);
uint8_t shutdown_request_active_get(void);

void shutdown_request_clear(void);

uint8_t shutdown_request_is_initialized(void);
uint8_t shutdown_request_is_complete(void);
uint8_t shutdown_request_has_fault(void);
shutdown_request_state_t shutdown_request_get_state(void);
power_ctrl_result_t shutdown_request_get_last_power_result(void);
void shutdown_request_get_status(shutdown_request_status_t *status);


shutdown_request_rearm_result_t shutdown_request_rearm(void);


void shutdown_request_force_off(void);

#ifdef __cplusplus
}
#endif

#endif 
