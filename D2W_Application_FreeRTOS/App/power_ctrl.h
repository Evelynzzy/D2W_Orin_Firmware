#ifndef POWER_CTRL_H
#define POWER_CTRL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef POWER_CTRL_CARRIER_ONLY_TEST_MODE
#define POWER_CTRL_CARRIER_ONLY_TEST_MODE    0U
#endif

#if ((POWER_CTRL_CARRIER_ONLY_TEST_MODE != 0U) && (POWER_CTRL_CARRIER_ONLY_TEST_MODE != 1U))
#error "POWER_CTRL_CARRIER_ONLY_TEST_MODE must be 0 or 1"
#endif

#ifndef POWER_CTRL_STARTUP_GUARD_MS
#define POWER_CTRL_STARTUP_GUARD_MS    100U
#endif

#ifndef POWER_CTRL_VDD_STABLE_DELAY_MS
#define POWER_CTRL_VDD_STABLE_DELAY_MS    75U
#endif

#ifndef POWER_CTRL_SYS_RESET_RELEASE_TIMEOUT_MS
#define POWER_CTRL_SYS_RESET_RELEASE_TIMEOUT_MS    1000U
#endif

#ifndef POWER_CTRL_SYS_RESET_ASSERT_TIMEOUT_MS
#define POWER_CTRL_SYS_RESET_ASSERT_TIMEOUT_MS    1000U
#endif

#ifndef POWER_CTRL_PERIPHERAL_SETTLE_MS
#define POWER_CTRL_PERIPHERAL_SETTLE_MS    5U
#endif

#ifndef POWER_CTRL_RESET_PULSE_MS
#define POWER_CTRL_RESET_PULSE_MS    5U
#endif

#ifndef POWER_CTRL_RESET_FEEDBACK_TIMEOUT_MS
#define POWER_CTRL_RESET_FEEDBACK_TIMEOUT_MS    50U
#endif

#ifndef POWER_CTRL_VDD_OFF_HOLD_MS
#define POWER_CTRL_VDD_OFF_HOLD_MS    10U
#endif

#ifndef POWER_CTRL_INPUT_STABLE_SAMPLES
#define POWER_CTRL_INPUT_STABLE_SAMPLES    3U
#endif

typedef enum
{
	POWER_CTRL_STATE_UNINITIALIZED = 0,
	POWER_CTRL_STATE_OFF,
	POWER_CTRL_STATE_POWERING_ON,
	POWER_CTRL_STATE_ON,
	POWER_CTRL_STATE_POWERING_OFF,
	POWER_CTRL_STATE_FAULT
} power_ctrl_state_t;

typedef enum
{
	POWER_CTRL_RESULT_OK = 0,
	POWER_CTRL_RESULT_NOT_INITIALIZED,
	POWER_CTRL_RESULT_BUSY,
	POWER_CTRL_RESULT_INVALID_STATE,
	POWER_CTRL_RESULT_SHUTDOWN_ACTIVE,
	POWER_CTRL_RESULT_SYS_RESET_RELEASE_TIMEOUT,
	POWER_CTRL_RESULT_SYS_RESET_ASSERT_TIMEOUT
} power_ctrl_result_t;

typedef struct
{
	power_ctrl_state_t state;
	power_ctrl_result_t last_result;

	uint8_t initialized;
	uint8_t orin_vdd_enabled;
	uint8_t orin_power_en_enabled;
	uint8_t sys_reset_control_asserted;
	uint8_t sys_reset_active;
	uint8_t shutdown_request_active;
	uint8_t usb_vbus_enabled;

	uint8_t peripheral_5v_enabled;
	uint8_t peripheral_3v3_enabled;
	uint8_t ch634_3v3_enabled;
	uint8_t connector_3v3_enabled;
} power_ctrl_status_t;

extern volatile uint32_t g_power_diag_wait_ms;
extern volatile uint8_t g_power_diag_pb14_latch;
extern volatile uint8_t g_power_diag_pb14_pin;
extern volatile uint8_t g_power_diag_pa7_latch;
extern volatile uint8_t g_power_diag_pa7_pin;
extern volatile uint8_t g_power_diag_pb15_latch;
extern volatile uint8_t g_power_diag_pb15_pin;
extern volatile uint8_t g_power_diag_pa8_pin;
extern volatile uint8_t g_power_diag_pa8_ever_high;

void power_gpio_init(void);

power_ctrl_result_t orin_power_on(void);
power_ctrl_result_t orin_power_off(void);

power_ctrl_result_t orin_shutdown_prepare(void);

power_ctrl_result_t orin_main_power_off(void);

void power_ctrl_force_off(void);

power_ctrl_result_t peripheral_power_on(void);
void peripheral_power_off(void);

void orin_reset_assert(void);
void orin_reset_release(void);
power_ctrl_result_t orin_reset_pulse(void);

void orin_power_en_enable(void);
void orin_power_en_disable(void);

uint8_t shutdown_request_is_active(void);
uint8_t usb_vbus_enable_is_active(void);
uint8_t orin_reset_status_is_active(void);

uint8_t power_ctrl_is_initialized(void);
power_ctrl_state_t power_ctrl_get_state(void);
power_ctrl_result_t power_ctrl_get_last_result(void);
void power_ctrl_get_status(power_ctrl_status_t *status);

#ifdef __cplusplus
}
#endif

#endif
