#ifndef POWER_FAIL_H
#define POWER_FAIL_H

#include <stdint.h>

#define POWER_FAIL_FLASH_PAGE_SIZE    (0x00000800U)
#define POWER_FAIL_FLASH_PAGE_A_ADDR    (0x0803F000U)
#define POWER_FAIL_FLASH_PAGE_B_ADDR    (0x0803F800U)
#define POWER_FAIL_FLASH_END_EXCLUSIVE    (0x08040000U)

#define POWER_FAIL_FLASH_DENSITY_ADDR    (0x1FFFF7E0U)
#define POWER_FAIL_MINIMUM_FLASH_KB    (256U)

#define POWER_FAIL_RECORD_SIZE    (128U)
#define POWER_FAIL_RECORDS_PER_PAGE    (POWER_FAIL_FLASH_PAGE_SIZE / POWER_FAIL_RECORD_SIZE)
#define POWER_FAIL_RECORD_FORMAT_VERSION    (1U)

#ifndef POWER_FAIL_DEFINE_SHARED_IRQ_HANDLER
#define POWER_FAIL_DEFINE_SHARED_IRQ_HANDLER    (0U)
#endif

#ifndef POWER_FAIL_FORWARD_WATCHDOG_IRQ
#define POWER_FAIL_FORWARD_WATCHDOG_IRQ    (0U)
#endif

#ifndef POWER_FAIL_IRQ_PRIORITY
#define POWER_FAIL_IRQ_PRIORITY    (5U)
#endif

#ifndef POWER_FAIL_REARM_STABLE_MS
#define POWER_FAIL_REARM_STABLE_MS    (100U)
#endif

#ifndef POWER_FAIL_ENABLE_SOFTWARE_TEST
#define POWER_FAIL_ENABLE_SOFTWARE_TEST    (1U)
#endif

typedef enum
{
	POWER_FAIL_RESULT_OK = 0,
	POWER_FAIL_RESULT_NOT_INITIALIZED,
	POWER_FAIL_RESULT_INVALID_ARGUMENT,
	POWER_FAIL_RESULT_FLASH_SIZE_MISMATCH,
	POWER_FAIL_RESULT_POWER_UNSTABLE,
	POWER_FAIL_RESULT_STORAGE_BUSY,
	POWER_FAIL_RESULT_NO_ERASED_SLOT,
	POWER_FAIL_RESULT_ERASE_FAILED,
	POWER_FAIL_RESULT_PROGRAM_FAILED,
	POWER_FAIL_RESULT_VERIFY_FAILED,
	POWER_FAIL_RESULT_CRC_FAILED,
	POWER_FAIL_RESULT_ALREADY_LATCHED,
	POWER_FAIL_RESULT_TEST_DISABLED
} power_fail_result_t;

typedef enum
{
	POWER_FAIL_STATE_UNINITIALIZED = 0,
	POWER_FAIL_STATE_READY,
	POWER_FAIL_STATE_SAVING,
	POWER_FAIL_STATE_SAVED,
	POWER_FAIL_STATE_PREPARING,
	POWER_FAIL_STATE_NO_SPACE,
	POWER_FAIL_STATE_ERROR
} power_fail_state_t;

typedef enum
{
	POWER_FAIL_SOURCE_NONE = 0,
	POWER_FAIL_SOURCE_PB8_EXTI,
	POWER_FAIL_SOURCE_POLLING_FALLBACK,
	POWER_FAIL_SOURCE_ACTIVE_AT_BOOT,
	POWER_FAIL_SOURCE_SOFTWARE_TEST
} power_fail_source_t;

typedef struct
{
	uint32_t uptime_ms;
	uint32_t reset_flags;
	uint32_t power_state;
	uint32_t shutdown_state;
	uint32_t watchdog_state;
	uint32_t watchdog_feed_count;
	uint32_t voltage_fault_flags;
	uint32_t orin_vdd_mv;
	uint32_t system_5v_mv;
	uint32_t system_3v3_mv;
	uint32_t system_1v8_mv;
	uint32_t user_words[5];
} power_fail_snapshot_t;

typedef struct
{
	uint32_t magic;
	uint16_t format_version;
	uint16_t record_size;
	uint32_t sequence;
	uint32_t event_count;
	uint32_t source;
	uint32_t pb8_level;
	power_fail_snapshot_t snapshot;
	uint32_t reserved[8];
	uint32_t crc32;
	uint32_t commit;
} power_fail_record_t;

extern volatile uint8_t power_fail_pin_state;
extern volatile uint8_t power_fail_active;
extern volatile uint8_t power_fail_pending;
extern volatile uint32_t power_fail_event_count;

extern volatile uint8_t power_fail_test_enable;
extern volatile uint8_t power_fail_test_pin_state;

extern volatile power_fail_result_t g_power_fail_init_result;
extern volatile power_fail_result_t g_power_fail_last_result;
extern volatile power_fail_state_t g_power_fail_state;
extern volatile power_fail_source_t g_power_fail_last_source;

extern volatile uint8_t g_power_fail_storage_ready;
extern volatile uint8_t g_power_fail_record_available;
extern volatile uint8_t g_power_fail_flash_operation_active;
extern volatile uint8_t g_power_fail_deferred_save_pending;
extern volatile uint8_t g_power_fail_test_request;
extern volatile uint8_t g_power_fail_test_rearm_request;

extern volatile uint32_t g_power_fail_flash_kb;
extern volatile uint32_t g_power_fail_latest_sequence;
extern volatile uint32_t g_power_fail_next_write_address;
extern volatile uint32_t g_power_fail_last_write_address;
extern volatile uint32_t g_power_fail_irq_count;
extern volatile uint32_t g_power_fail_deferred_save_count;
extern volatile uint32_t g_power_fail_deferred_save_complete_count;
extern volatile uint32_t g_power_fail_saved_count;
extern volatile uint32_t g_power_fail_write_error_count;
extern volatile uint32_t g_power_fail_crc_error_count;
extern volatile uint32_t g_power_fail_invalid_record_count;
extern volatile uint32_t g_power_fail_last_fmc_state;

extern volatile power_fail_snapshot_t g_power_fail_snapshot;
extern volatile power_fail_record_t g_power_fail_latest_record;

power_fail_result_t power_fail_init(void);
void power_fail_task(void);

void power_fail_update(void);

void power_fail_snapshot_update(const power_fail_snapshot_t *snapshot);
uint8_t power_fail_latest_record_get(power_fail_record_t *record);

uint8_t power_fail_pending_get(void);
uint8_t power_fail_active_get(void);
void power_fail_clear(void);
void power_fail_rearm(void);

power_fail_result_t power_fail_storage_prepare(void);
power_fail_result_t power_fail_storage_format(void);

power_fail_result_t power_fail_trigger_software_test(void);

void power_fail_shared_exti5_9_irq_handler(void);

#if (POWER_FAIL_DEFINE_SHARED_IRQ_HANDLER == 1U)
void EXTI5_9_IRQHandler(void);
#endif

#endif
