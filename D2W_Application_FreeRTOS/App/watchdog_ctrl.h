#ifndef WATCHDOG_CTRL_H
#define WATCHDOG_CTRL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


#ifndef WATCHDOG_CTRL_FEED_PERIOD_MS
#define WATCHDOG_CTRL_FEED_PERIOD_MS    200U
#endif

#ifndef WATCHDOG_CTRL_FIRST_EDGE_LOW_MS
#define WATCHDOG_CTRL_FIRST_EDGE_LOW_MS    10U
#endif

#ifndef WATCHDOG_CTRL_FIRST_EDGE_HIGH_MS
#define WATCHDOG_CTRL_FIRST_EDGE_HIGH_MS    10U
#endif

#ifndef WATCHDOG_CTRL_WDO_RELEASE_TIMEOUT_MS
#define WATCHDOG_CTRL_WDO_RELEASE_TIMEOUT_MS    5U
#endif

#ifndef WATCHDOG_CTRL_AUTO_RESET_ON_TIMEOUT
#define WATCHDOG_CTRL_AUTO_RESET_ON_TIMEOUT    1U
#endif

#ifndef WATCHDOG_CTRL_USE_WDO_EXTI
#define WATCHDOG_CTRL_USE_WDO_EXTI    1U
#endif


#ifndef WATCHDOG_CTRL_DEFINE_IRQ_HANDLER
#define WATCHDOG_CTRL_DEFINE_IRQ_HANDLER    0U
#endif

#ifndef WATCHDOG_CTRL_IRQ_PRIORITY
#define WATCHDOG_CTRL_IRQ_PRIORITY    5U
#endif


#ifndef WATCHDOG_CTRL_TEST_STOP_REQUEST_VALUE
#define WATCHDOG_CTRL_TEST_STOP_REQUEST_VALUE    1U
#endif

#ifndef WATCHDOG_CTRL_TEST_RESUME_REQUEST_VALUE
#define WATCHDOG_CTRL_TEST_RESUME_REQUEST_VALUE    1U
#endif

typedef enum
{
	WATCHDOG_CTRL_STATE_UNINITIALIZED = 0,
	WATCHDOG_CTRL_STATE_RUNNING,
	WATCHDOG_CTRL_STATE_FEED_PAUSED,
	WATCHDOG_CTRL_STATE_TIMEOUT_DETECTED,
	WATCHDOG_CTRL_STATE_FAULT
} watchdog_ctrl_state_t;

typedef enum
{
	WATCHDOG_CTRL_RESULT_OK = 0,
	WATCHDOG_CTRL_RESULT_NOT_INITIALIZED,
	WATCHDOG_CTRL_RESULT_INVALID_STATE,
	WATCHDOG_CTRL_RESULT_TIMEOUT,
	WATCHDOG_CTRL_RESULT_WDO_STUCK_LOW
} watchdog_ctrl_result_t;

typedef struct
{
	watchdog_ctrl_state_t state;
	watchdog_ctrl_result_t last_result;

	uint8_t initialized;
	uint8_t feeding_enabled;
	uint8_t wdi_level;
	uint8_t wdo_level;
	uint8_t timeout_active;
	uint8_t timeout_latched;
	uint8_t auto_reset_enabled;

	uint32_t feed_count;
	uint32_t timeout_count;
	uint32_t irq_count;
	uint32_t polling_recovery_count;
	uint32_t task_call_count;
	uint32_t elapsed_since_feed_ms;
} watchdog_ctrl_status_t;


extern volatile uint32_t g_watchdog_feed_count;
extern volatile uint8_t g_watchdog_wdi_level;
extern volatile uint8_t g_watchdog_timeout;

extern volatile uint8_t g_watchdog_wdo_level;
extern volatile uint8_t g_watchdog_timeout_latched;
extern volatile watchdog_ctrl_state_t g_watchdog_state;
extern volatile watchdog_ctrl_result_t g_watchdog_last_result;
extern volatile uint32_t g_watchdog_timeout_count;
extern volatile uint32_t g_watchdog_irq_count;
extern volatile uint32_t g_watchdog_polling_recovery_count;
extern volatile uint32_t g_watchdog_task_call_count;
extern volatile uint32_t g_watchdog_elapsed_since_feed_ms;

extern volatile uint8_t g_watchdog_test_stop_request;
extern volatile uint8_t g_watchdog_test_resume_request;


watchdog_ctrl_result_t external_watchdog_init(void);

void external_watchdog_task_1ms(void);
void external_watchdog_task(void);

watchdog_ctrl_result_t external_watchdog_feed_now(void);

void external_watchdog_wdo_irq_handler(void);

uint8_t external_watchdog_timeout_is_active(void);
uint8_t external_watchdog_is_initialized(void);
uint8_t external_watchdog_is_running(void);
watchdog_ctrl_state_t external_watchdog_get_state(void);
watchdog_ctrl_result_t external_watchdog_get_last_result(void);
void external_watchdog_get_status(watchdog_ctrl_status_t *status);

watchdog_ctrl_result_t external_watchdog_stop_feeding_for_test(void);
watchdog_ctrl_result_t external_watchdog_resume_feeding(void);

#ifdef __cplusplus
}
#endif

#endif 
