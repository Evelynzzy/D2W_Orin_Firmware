#ifndef ORIN_STATUS_H
#define ORIN_STATUS_H

#include <stdint.h>

#include "power_ctrl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ORIN_STATUS_VERSION_MAJOR    (1U)
#define ORIN_STATUS_VERSION_MINOR    (0U)
#define ORIN_STATUS_VERSION_PATCH    (0U)

#ifndef ORIN_STATUS_RESET_ASSERT_DEBOUNCE_SAMPLES
#define ORIN_STATUS_RESET_ASSERT_DEBOUNCE_SAMPLES    (3U)
#endif

#ifndef ORIN_STATUS_RESET_RELEASE_DEBOUNCE_SAMPLES
#define ORIN_STATUS_RESET_RELEASE_DEBOUNCE_SAMPLES    (5U)
#endif

#ifndef ORIN_STATUS_RUNNING_CONFIRM_MS
#define ORIN_STATUS_RUNNING_CONFIRM_MS    (20U)
#endif

#ifndef ORIN_STATUS_RESET_STUCK_TIMEOUT_MS
#define ORIN_STATUS_RESET_STUCK_TIMEOUT_MS    (5000U)
#endif

#ifndef ORIN_STATUS_ENABLE_SOFTWARE_TEST
#define ORIN_STATUS_ENABLE_SOFTWARE_TEST    (1U)
#endif

#define ORIN_STATUS_TEST_ENABLE_KEY    (0xA5U)

#define ORIN_STATUS_FAULT_NONE    (0x00000000UL)
#define ORIN_STATUS_FAULT_RESET_STUCK_ACTIVE    (0x00000001UL)
#define ORIN_STATUS_FAULT_POWER_CTRL    (0x00000002UL)

typedef enum
{
	ORIN_STATUS_STATE_UNINITIALIZED = 0,
	ORIN_STATUS_STATE_POWER_OFF,
	ORIN_STATUS_STATE_POWERING_ON,
	ORIN_STATUS_STATE_BOOTING,
	ORIN_STATUS_STATE_RUNNING,
	ORIN_STATUS_STATE_RESET_ACTIVE,
	ORIN_STATUS_STATE_POWERING_OFF,
	ORIN_STATUS_STATE_FAULT
} orin_status_state_t;

typedef enum
{
	ORIN_STATUS_RESULT_OK = 0,
	ORIN_STATUS_RESULT_NOT_INITIALIZED,
	ORIN_STATUS_RESULT_INVALID_ARGUMENT,
	ORIN_STATUS_RESULT_CONFIG_ERROR,
	ORIN_STATUS_RESULT_POWER_CTRL_NOT_INITIALIZED
} orin_status_result_t;

typedef struct
{
	orin_status_state_t state;
	orin_status_result_t init_result;
	orin_status_result_t last_result;
	power_ctrl_state_t power_state;

	uint8_t initialized;
	uint8_t raw_pin_level;
	uint8_t raw_reset_active;
	uint8_t filtered_reset_active;
	uint8_t running;
	uint8_t fault_active;
	uint8_t reset_control_asserted;
	uint8_t reserved_u8;

	uint32_t fault_flags;
	uint32_t latched_fault_flags;

	uint32_t reset_event_count;
	uint32_t reset_release_event_count;
	uint32_t commanded_reset_event_count;
	uint32_t autonomous_reset_event_count;
	uint32_t raw_edge_count;
	uint32_t rejected_glitch_count;
	uint32_t state_change_count;
	uint32_t fault_event_count;
	uint32_t recovery_event_count;
	uint32_t task_call_count;

	uint32_t last_update_ms;
	uint32_t last_state_change_ms;
	uint32_t last_reset_assert_ms;
	uint32_t last_reset_release_ms;
	uint32_t last_fault_ms;
	uint32_t last_recovery_ms;
	uint32_t reset_active_duration_ms;
	uint32_t running_duration_ms;
} orin_status_snapshot_t;

extern volatile uint8_t orin_reset_pin_state;
extern volatile uint8_t orin_reset_active;
extern volatile uint8_t orin_running_state;
extern volatile uint32_t orin_reset_event_count;

extern volatile uint8_t orin_status_test_enable;
extern volatile uint8_t orin_status_test_pin_state;

extern volatile orin_status_state_t g_orin_status_state;
extern volatile orin_status_result_t g_orin_status_init_result;
extern volatile orin_status_result_t g_orin_status_last_result;
extern volatile power_ctrl_state_t g_orin_status_power_state;

extern volatile uint8_t g_orin_status_initialized;
extern volatile uint8_t g_orin_status_raw_reset_active;
extern volatile uint8_t g_orin_status_fault_active;
extern volatile uint8_t g_orin_status_reset_control_asserted;

extern volatile uint32_t g_orin_status_fault_flags;
extern volatile uint32_t g_orin_status_latched_fault_flags;

extern volatile uint32_t g_orin_status_reset_release_event_count;
extern volatile uint32_t g_orin_status_commanded_reset_event_count;
extern volatile uint32_t g_orin_status_autonomous_reset_event_count;
extern volatile uint32_t g_orin_status_raw_edge_count;
extern volatile uint32_t g_orin_status_rejected_glitch_count;
extern volatile uint32_t g_orin_status_state_change_count;
extern volatile uint32_t g_orin_status_fault_event_count;
extern volatile uint32_t g_orin_status_recovery_event_count;
extern volatile uint32_t g_orin_status_task_call_count;

extern volatile uint32_t g_orin_status_last_update_ms;
extern volatile uint32_t g_orin_status_last_state_change_ms;
extern volatile uint32_t g_orin_status_last_reset_assert_ms;
extern volatile uint32_t g_orin_status_last_reset_release_ms;
extern volatile uint32_t g_orin_status_last_fault_ms;
extern volatile uint32_t g_orin_status_last_recovery_ms;
extern volatile uint32_t g_orin_status_reset_active_duration_ms;
extern volatile uint32_t g_orin_status_running_duration_ms;

void orin_status_init(void);
orin_status_result_t orin_status_init_checked(void);

void orin_status_task_1ms(void);

void orin_status_update(void);

uint8_t orin_reset_is_active(void);
uint8_t orin_is_running(void);
uint32_t orin_reset_event_count_get(void);

uint8_t orin_status_is_initialized(void);
uint8_t orin_status_is_healthy(void);
orin_status_state_t orin_status_get_state(void);
orin_status_result_t orin_status_get_last_result(void);
uint32_t orin_status_get_fault_flags(void);

void orin_status_clear_latched_faults(void);
void orin_status_get_snapshot(orin_status_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif
