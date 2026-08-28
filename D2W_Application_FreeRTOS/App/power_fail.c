
#include "gd32f30x_fmc.h"
#include "board_pins.h"
#include "systick.h"
#include "power_fail.h"

#include <stddef.h>

#define POWER_FAIL_RECORD_MAGIC    (0x474C4650U)
#define POWER_FAIL_RECORD_COMMIT    (0xA55A3CC3U)
#define POWER_FAIL_ERASED_WORD    (0xFFFFFFFFU)
#define POWER_FAIL_CRC32_POLYNOMIAL    (0xEDB88320U)
#define POWER_FAIL_WORDS_PER_RECORD    (POWER_FAIL_RECORD_SIZE / (uint32_t)sizeof(uint32_t))
#define POWER_FAIL_WORDS_PER_PAGE    (POWER_FAIL_FLASH_PAGE_SIZE / (uint32_t)sizeof(uint32_t))

#if (POWER_FAIL_RECORD_SIZE != 128U)
#error "POWER_FAIL_RECORD_SIZE must remain 128 bytes for record compatibility"
#endif

#if ((POWER_FAIL_FLASH_PAGE_SIZE % POWER_FAIL_RECORD_SIZE) != 0U)
#error "The Flash page size must be an integer multiple of the record size"
#endif

#if (POWER_FAIL_FLASH_PAGE_B_ADDR != (POWER_FAIL_FLASH_PAGE_A_ADDR + POWER_FAIL_FLASH_PAGE_SIZE))
#error "Power-fail Flash pages must be adjacent"
#endif

#if (POWER_FAIL_FLASH_END_EXCLUSIVE != (POWER_FAIL_FLASH_PAGE_B_ADDR + POWER_FAIL_FLASH_PAGE_SIZE))
#error "Power-fail Flash end address is inconsistent"
#endif

typedef char power_fail_snapshot_size_must_be_64_bytes[(sizeof(power_fail_snapshot_t) == 64U) ? 1 : -1];
typedef char power_fail_record_size_must_be_128_bytes[(sizeof(power_fail_record_t) == POWER_FAIL_RECORD_SIZE) ? 1 : -1];
typedef char power_fail_crc_offset_must_be_120[(offsetof(power_fail_record_t, crc32) == 120U) ? 1 : -1];
typedef char power_fail_commit_offset_must_be_124[(offsetof(power_fail_record_t, commit) == 124U) ? 1 : -1];

volatile uint8_t power_fail_pin_state = 1U;
volatile uint8_t power_fail_active = 0U;
volatile uint8_t power_fail_pending = 0U;
volatile uint32_t power_fail_event_count = 0U;

volatile uint8_t power_fail_test_enable = 0U;
volatile uint8_t power_fail_test_pin_state = 1U;

volatile power_fail_result_t g_power_fail_init_result = POWER_FAIL_RESULT_NOT_INITIALIZED;
volatile power_fail_result_t g_power_fail_last_result = POWER_FAIL_RESULT_NOT_INITIALIZED;
volatile power_fail_state_t g_power_fail_state = POWER_FAIL_STATE_UNINITIALIZED;
volatile power_fail_source_t g_power_fail_last_source = POWER_FAIL_SOURCE_NONE;

volatile uint8_t g_power_fail_storage_ready = 0U;
volatile uint8_t g_power_fail_record_available = 0U;
volatile uint8_t g_power_fail_flash_operation_active = 0U;
volatile uint8_t g_power_fail_deferred_save_pending = 0U;
volatile uint8_t g_power_fail_test_request = 0U;
volatile uint8_t g_power_fail_test_rearm_request = 0U;

volatile uint32_t g_power_fail_flash_kb = 0U;
volatile uint32_t g_power_fail_latest_sequence = 0U;
volatile uint32_t g_power_fail_next_write_address = 0U;
volatile uint32_t g_power_fail_last_write_address = 0U;
volatile uint32_t g_power_fail_irq_count = 0U;
volatile uint32_t g_power_fail_deferred_save_count = 0U;
volatile uint32_t g_power_fail_deferred_save_complete_count = 0U;
volatile uint32_t g_power_fail_saved_count = 0U;
volatile uint32_t g_power_fail_write_error_count = 0U;
volatile uint32_t g_power_fail_crc_error_count = 0U;
volatile uint32_t g_power_fail_invalid_record_count = 0U;
volatile uint32_t g_power_fail_last_fmc_state = (uint32_t)FMC_READY;

volatile power_fail_snapshot_t g_power_fail_snapshot;
volatile power_fail_record_t g_power_fail_latest_record;

static volatile uint8_t s_initialized = 0U;
static volatile uint8_t s_event_latched = 0U;
static uint8_t s_previous_pin_state = 1U;
static uint8_t s_inactive_timing = 0U;
static uint32_t s_inactive_since_ms = 0U;
static uint32_t s_latest_record_address = 0U;
static volatile uint8_t s_save_retry_pending = 0U;
static volatile power_fail_source_t s_save_retry_source = POWER_FAIL_SOURCE_NONE;
static volatile power_fail_source_t s_deferred_save_source = POWER_FAIL_SOURCE_NONE;

static power_fail_result_t power_fail_emergency_save(power_fail_source_t source);

#if (POWER_FAIL_FORWARD_WATCHDOG_IRQ == 1U)

extern void external_watchdog_wdo_irq_handler(void);
#endif

static uint8_t power_fail_hardware_pin_read(void)
{
	return (uint8_t)gpio_input_bit_get(POWER_FAIL_PORT, POWER_FAIL_PIN);
}

static uint8_t power_fail_effective_pin_read(void)
{
	if (power_fail_test_enable != 0U)
	{
		return (power_fail_test_pin_state != 0U) ? 1U : 0U;
	}

	return power_fail_hardware_pin_read();
}

static uint8_t power_fail_active_from_pin(uint8_t pin_state)
{
	return (pin_state == POWER_FAIL_ACTIVE_LEVEL) ? 1U : 0U;
}

static uint8_t power_fail_hardware_is_active(void)
{
	return power_fail_active_from_pin(power_fail_hardware_pin_read());
}

static void power_fail_fmc_flags_clear(void)
{
	fmc_flag_clear(FMC_FLAG_BANK0_END);
	fmc_flag_clear(FMC_FLAG_BANK0_WPERR);
	fmc_flag_clear(FMC_FLAG_BANK0_PGERR);
}

static uint32_t power_fail_crc32_calculate(const volatile uint8_t *data, uint32_t length)
{
	uint32_t crc = 0xFFFFFFFFU;
	uint32_t index;
	uint32_t bit_index;

	for (index = 0U; index < length; index++)
	{
		crc ^= (uint32_t)data[index];

		for (bit_index = 0U; bit_index < 8U; bit_index++)
		{
			if ((crc & 1U) != 0U)
			{
				crc = (crc >> 1U) ^ POWER_FAIL_CRC32_POLYNOMIAL;
			}
			else
			{
				crc >>= 1U;
			}
		}
	}

	return ~crc;
}

static uint8_t power_fail_sequence_is_newer(uint32_t candidate, uint32_t reference)
{
	return (((int32_t)(candidate - reference)) > 0) ? 1U : 0U;
}

static uint32_t power_fail_page_base_from_address(uint32_t address)
{
	if ((address >= POWER_FAIL_FLASH_PAGE_A_ADDR) && (address < (POWER_FAIL_FLASH_PAGE_A_ADDR + POWER_FAIL_FLASH_PAGE_SIZE)))
	{
		return POWER_FAIL_FLASH_PAGE_A_ADDR;
	}

	if ((address >= POWER_FAIL_FLASH_PAGE_B_ADDR) && (address < POWER_FAIL_FLASH_END_EXCLUSIVE))
	{
		return POWER_FAIL_FLASH_PAGE_B_ADDR;
	}

	return 0U;
}

static uint8_t power_fail_record_address_is_valid(uint32_t address)
{
	uint32_t page_base = power_fail_page_base_from_address(address);

	if (page_base == 0U)
	{
		return 0U;
	}

	if (((address - page_base) % POWER_FAIL_RECORD_SIZE) != 0U)
	{
		return 0U;
	}

	return 1U;
}

static uint8_t power_fail_record_slot_is_erased(uint32_t address)
{
	const volatile uint32_t *words;
	uint32_t index;

	if (power_fail_record_address_is_valid(address) == 0U)
	{
		return 0U;
	}

	words = (const volatile uint32_t *)address;

	for (index = 0U; index < POWER_FAIL_WORDS_PER_RECORD; index++)
	{
		if (words[index] != POWER_FAIL_ERASED_WORD)
		{
			return 0U;
		}
	}

	return 1U;
}

static uint8_t power_fail_page_is_erased(uint32_t page_address)
{
	const volatile uint32_t *words = (const volatile uint32_t *)page_address;
	uint32_t index;

	for (index = 0U; index < POWER_FAIL_WORDS_PER_PAGE; index++)
	{
		if (words[index] != POWER_FAIL_ERASED_WORD)
		{
			return 0U;
		}
	}

	return 1U;
}

static uint8_t power_fail_record_header_is_valid(const volatile power_fail_record_t *record)
{
	if (record->magic != POWER_FAIL_RECORD_MAGIC)
	{
		return 0U;
	}

	if (record->format_version != POWER_FAIL_RECORD_FORMAT_VERSION)
	{
		return 0U;
	}

	if (record->record_size != POWER_FAIL_RECORD_SIZE)
	{
		return 0U;
	}

	if (record->sequence == 0U)
	{
		return 0U;
	}

	if (record->commit != POWER_FAIL_RECORD_COMMIT)
	{
		return 0U;
	}

	return 1U;
}

static uint8_t power_fail_record_crc_is_valid(const volatile power_fail_record_t *record)
{
	uint32_t calculated_crc;

	calculated_crc = power_fail_crc32_calculate((const volatile uint8_t *)record, (uint32_t)offsetof(power_fail_record_t, crc32));

	return (calculated_crc == record->crc32) ? 1U : 0U;
}

static uint8_t power_fail_record_is_valid(const volatile power_fail_record_t *record)
{
	if (power_fail_record_header_is_valid(record) == 0U)
	{
		return 0U;
	}

	return power_fail_record_crc_is_valid(record);
}

static void power_fail_record_public_clear(void)
{
	volatile uint32_t *destination = (volatile uint32_t *)&g_power_fail_latest_record;
	uint32_t index;

	for (index = 0U; index < POWER_FAIL_WORDS_PER_RECORD; index++)
	{
		destination[index] = 0U;
	}
}

static void power_fail_record_copy_to_public(const volatile power_fail_record_t *source)
{
	volatile uint32_t *destination = (volatile uint32_t *)&g_power_fail_latest_record;
	const volatile uint32_t *source_words = (const volatile uint32_t *)source;
	uint32_t index;

	for (index = 0U; index < POWER_FAIL_WORDS_PER_RECORD; index++)
	{
		destination[index] = source_words[index];
	}
}

static void power_fail_storage_scan(void)
{
	const uint32_t page_addresses[2] =
	{
		POWER_FAIL_FLASH_PAGE_A_ADDR,
		POWER_FAIL_FLASH_PAGE_B_ADDR
	};
	uint32_t page_index;
	uint32_t slot_index;
	uint32_t address;
	const volatile power_fail_record_t *record;
	uint8_t found = 0U;
	uint32_t latest_sequence = 0U;
	uint32_t latest_address = 0U;

	g_power_fail_record_available = 0U;
	g_power_fail_latest_sequence = 0U;
	g_power_fail_crc_error_count = 0U;
	g_power_fail_invalid_record_count = 0U;
	s_latest_record_address = 0U;
	power_fail_record_public_clear();

	for (page_index = 0U; page_index < 2U; page_index++)
	{
		for (slot_index = 0U; slot_index < POWER_FAIL_RECORDS_PER_PAGE; slot_index++)
		{
			address = page_addresses[page_index] + (slot_index * POWER_FAIL_RECORD_SIZE);

			if (power_fail_record_slot_is_erased(address) != 0U)
			{
				continue;
			}

			record = (const volatile power_fail_record_t *)address;

			if (power_fail_record_header_is_valid(record) == 0U)
			{
				g_power_fail_invalid_record_count++;
				continue;
			}

			if (power_fail_record_crc_is_valid(record) == 0U)
			{
				g_power_fail_crc_error_count++;
				g_power_fail_invalid_record_count++;
				continue;
			}

			if ((found == 0U) || (power_fail_sequence_is_newer(record->sequence, latest_sequence) != 0U))
			{
				found = 1U;
				latest_sequence = record->sequence;
				latest_address = address;
				power_fail_record_copy_to_public(record);
			}
		}
	}

	if (found != 0U)
	{
		g_power_fail_record_available = 1U;
		g_power_fail_latest_sequence = latest_sequence;
		s_latest_record_address = latest_address;
	}
}

static uint32_t power_fail_find_erased_slot_in_range(uint32_t page_address, uint32_t first_slot, uint32_t last_slot)
{
	uint32_t slot_index;
	uint32_t address;

	if ((first_slot >= POWER_FAIL_RECORDS_PER_PAGE) || (last_slot > POWER_FAIL_RECORDS_PER_PAGE) || (first_slot >= last_slot))
	{
		return 0U;
	}

	for (slot_index = first_slot; slot_index < last_slot; slot_index++)
	{
		address = page_address + (slot_index * POWER_FAIL_RECORD_SIZE);

		if (power_fail_record_slot_is_erased(address) != 0U)
		{
			return address;
		}
	}

	return 0U;
}

static uint32_t power_fail_find_next_erased_slot(void)
{
	uint32_t latest_page;
	uint32_t other_page;
	uint32_t latest_slot;
	uint32_t address;

	if (g_power_fail_record_available == 0U)
	{
		address = power_fail_find_erased_slot_in_range(POWER_FAIL_FLASH_PAGE_A_ADDR, 0U, POWER_FAIL_RECORDS_PER_PAGE);

		if (address != 0U)
		{
			return address;
		}

		return power_fail_find_erased_slot_in_range(POWER_FAIL_FLASH_PAGE_B_ADDR, 0U, POWER_FAIL_RECORDS_PER_PAGE);
	}

	latest_page = power_fail_page_base_from_address(s_latest_record_address);
	latest_slot = (s_latest_record_address - latest_page) / POWER_FAIL_RECORD_SIZE;
	other_page = (latest_page == POWER_FAIL_FLASH_PAGE_A_ADDR) ? POWER_FAIL_FLASH_PAGE_B_ADDR :
				 POWER_FAIL_FLASH_PAGE_A_ADDR;

	address = power_fail_find_erased_slot_in_range(latest_page, latest_slot + 1U, POWER_FAIL_RECORDS_PER_PAGE);

	if (address != 0U)
	{
		return address;
	}

	address = power_fail_find_erased_slot_in_range(other_page, 0U, POWER_FAIL_RECORDS_PER_PAGE);

	if (address != 0U)
	{
		return address;
	}

	return power_fail_find_erased_slot_in_range(latest_page, 0U, latest_slot);
}

static power_fail_result_t power_fail_flash_page_erase(uint32_t page_address)
{
	fmc_state_enum fmc_state;

	if ((page_address != POWER_FAIL_FLASH_PAGE_A_ADDR) && (page_address != POWER_FAIL_FLASH_PAGE_B_ADDR))
	{
		return POWER_FAIL_RESULT_INVALID_ARGUMENT;
	}

	fmc_unlock();
	power_fail_fmc_flags_clear();

	fmc_state = fmc_page_erase(page_address);
	g_power_fail_last_fmc_state = (uint32_t)fmc_state;

	power_fail_fmc_flags_clear();
	fmc_lock();

	if (fmc_state != FMC_READY)
	{
		return POWER_FAIL_RESULT_ERASE_FAILED;
	}

	if (power_fail_page_is_erased(page_address) == 0U)
	{
		return POWER_FAIL_RESULT_VERIFY_FAILED;
	}

	return POWER_FAIL_RESULT_OK;
}

static power_fail_result_t power_fail_storage_prepare_internal(uint8_t allow_page_erase)
{
	uint32_t address;
	uint32_t erase_page;
	uint32_t latest_page;
	power_fail_result_t result;

	if ((g_power_fail_flash_operation_active != 0U) || (g_power_fail_deferred_save_pending != 0U))
	{
		return POWER_FAIL_RESULT_STORAGE_BUSY;
	}

	g_power_fail_flash_operation_active = 1U;
	g_power_fail_state = POWER_FAIL_STATE_PREPARING;

	power_fail_storage_scan();
	address = power_fail_find_next_erased_slot();

	if (address != 0U)
	{
		g_power_fail_next_write_address = address;
		g_power_fail_storage_ready = 1U;
		g_power_fail_state = POWER_FAIL_STATE_READY;

		g_power_fail_flash_operation_active = 0U;

		if (s_save_retry_pending != 0U)
		{
			power_fail_source_t retry_source = s_save_retry_source;
			s_save_retry_pending = 0U;
			s_save_retry_source = POWER_FAIL_SOURCE_NONE;
			return power_fail_emergency_save(retry_source);
		}

		return POWER_FAIL_RESULT_OK;
	}

	g_power_fail_next_write_address = 0U;
	g_power_fail_storage_ready = 0U;

	if (allow_page_erase == 0U)
	{
		g_power_fail_flash_operation_active = 0U;
		g_power_fail_state = POWER_FAIL_STATE_NO_SPACE;
		return POWER_FAIL_RESULT_NO_ERASED_SLOT;
	}

	if (power_fail_hardware_is_active() != 0U)
	{
		g_power_fail_flash_operation_active = 0U;
		g_power_fail_state = POWER_FAIL_STATE_NO_SPACE;
		return POWER_FAIL_RESULT_POWER_UNSTABLE;
	}

	if (g_power_fail_record_available != 0U)
	{
		latest_page = power_fail_page_base_from_address(s_latest_record_address);
		erase_page = (latest_page == POWER_FAIL_FLASH_PAGE_A_ADDR) ? POWER_FAIL_FLASH_PAGE_B_ADDR :
					 POWER_FAIL_FLASH_PAGE_A_ADDR;
	}
	else
	{
		erase_page = POWER_FAIL_FLASH_PAGE_A_ADDR;
	}

	result = power_fail_flash_page_erase(erase_page);
	g_power_fail_flash_operation_active = 0U;

	if (result != POWER_FAIL_RESULT_OK)
	{
		g_power_fail_state = POWER_FAIL_STATE_ERROR;
		return result;
	}

	g_power_fail_next_write_address = erase_page;
	g_power_fail_storage_ready = 1U;
	g_power_fail_state = POWER_FAIL_STATE_READY;

	if (s_save_retry_pending != 0U)
	{
		power_fail_source_t retry_source = s_save_retry_source;
		s_save_retry_pending = 0U;
		s_save_retry_source = POWER_FAIL_SOURCE_NONE;
		return power_fail_emergency_save(retry_source);
	}

	return POWER_FAIL_RESULT_OK;
}

static uint32_t power_fail_next_sequence_get(void)
{
	uint32_t sequence = g_power_fail_latest_sequence + 1U;

	if (sequence == 0U)
	{
		sequence = 1U;
	}

	return sequence;
}

static void power_fail_record_build(power_fail_record_t *record, power_fail_source_t source)
{
	uint32_t *record_words = (uint32_t *)record;
	uint32_t *snapshot_words = (uint32_t *)&record->snapshot;
	const volatile uint32_t *public_snapshot_words = (const volatile uint32_t *)&g_power_fail_snapshot;
	uint32_t index;

	for (index = 0U; index < POWER_FAIL_WORDS_PER_RECORD; index++)
	{
		record_words[index] = POWER_FAIL_ERASED_WORD;
	}

	record->magic = POWER_FAIL_RECORD_MAGIC;
	record->format_version = POWER_FAIL_RECORD_FORMAT_VERSION;
	record->record_size = POWER_FAIL_RECORD_SIZE;
	record->sequence = power_fail_next_sequence_get();
	record->event_count = power_fail_event_count;
	record->source = (uint32_t)source;
	record->pb8_level = (uint32_t)power_fail_pin_state;

	for (index = 0U; index < (sizeof(power_fail_snapshot_t) / sizeof(uint32_t)); index++)
	{
		snapshot_words[index] = public_snapshot_words[index];
	}

	record->crc32 = power_fail_crc32_calculate((const volatile uint8_t *)record, (uint32_t)offsetof(power_fail_record_t, crc32));
	record->commit = POWER_FAIL_RECORD_COMMIT;
}

static power_fail_result_t power_fail_flash_record_program(uint32_t address, const power_fail_record_t *record)
{
	const uint32_t *record_words = (const uint32_t *)record;
	fmc_state_enum fmc_state = FMC_READY;
	power_fail_result_t result = POWER_FAIL_RESULT_OK;
	uint32_t index;
	uint32_t target_address;

	if ((power_fail_record_address_is_valid(address) == 0U) || (record == (const power_fail_record_t *)0))
	{
		return POWER_FAIL_RESULT_INVALID_ARGUMENT;
	}

	if (power_fail_record_slot_is_erased(address) == 0U)
	{
		return POWER_FAIL_RESULT_NO_ERASED_SLOT;
	}

	fmc_unlock();
	power_fail_fmc_flags_clear();

	for (index = 0U; index < (POWER_FAIL_WORDS_PER_RECORD - 1U); index++)
	{
		if (record_words[index] == POWER_FAIL_ERASED_WORD)
		{
			continue;
		}

		target_address = address + (index * (uint32_t)sizeof(uint32_t));
		fmc_state = fmc_word_program(target_address, record_words[index]);
		g_power_fail_last_fmc_state = (uint32_t)fmc_state;
		power_fail_fmc_flags_clear();

		if (fmc_state != FMC_READY)
		{
			result = POWER_FAIL_RESULT_PROGRAM_FAILED;
			break;
		}

		if (*(const volatile uint32_t *)target_address != record_words[index])
		{
			result = POWER_FAIL_RESULT_VERIFY_FAILED;
			break;
		}
	}

	if (result == POWER_FAIL_RESULT_OK)
	{
		__DSB();
		target_address = address + (uint32_t)offsetof(power_fail_record_t, commit);
		fmc_state = fmc_word_program(target_address, POWER_FAIL_RECORD_COMMIT);
		g_power_fail_last_fmc_state = (uint32_t)fmc_state;
		power_fail_fmc_flags_clear();

		if (fmc_state != FMC_READY)
		{
			result = POWER_FAIL_RESULT_PROGRAM_FAILED;
		}
		else if (*(const volatile uint32_t *)target_address != POWER_FAIL_RECORD_COMMIT)
		{
			result = POWER_FAIL_RESULT_VERIFY_FAILED;
		}
	}

	fmc_lock();
	__DSB();

	if ((result == POWER_FAIL_RESULT_OK) && (power_fail_record_is_valid((const volatile power_fail_record_t *)address) == 0U))
	{
		result = POWER_FAIL_RESULT_CRC_FAILED;
	}

	return result;
}

static void power_fail_select_immediate_next_slot(uint32_t written_address)
{
	uint32_t page_base = power_fail_page_base_from_address(written_address);
	uint32_t page_end = page_base + POWER_FAIL_FLASH_PAGE_SIZE;
	uint32_t next_address = written_address + POWER_FAIL_RECORD_SIZE;

	if ((next_address < page_end) && (power_fail_record_slot_is_erased(next_address) != 0U))
	{
		g_power_fail_next_write_address = next_address;
		g_power_fail_storage_ready = 1U;
	}
	else
	{
		g_power_fail_next_write_address = 0U;
		g_power_fail_storage_ready = 0U;
	}
}

static power_fail_result_t power_fail_emergency_save(power_fail_source_t source)
{
	power_fail_record_t record;
	power_fail_result_t result;
	uint32_t write_address;

	if (s_initialized == 0U)
	{
		return POWER_FAIL_RESULT_NOT_INITIALIZED;
	}

	if (g_power_fail_flash_operation_active != 0U)
	{
		return POWER_FAIL_RESULT_STORAGE_BUSY;
	}

	if ((g_power_fail_storage_ready == 0U) || (power_fail_record_address_is_valid(g_power_fail_next_write_address) == 0U))
	{
		return POWER_FAIL_RESULT_NO_ERASED_SLOT;
	}

	write_address = g_power_fail_next_write_address;
	g_power_fail_last_write_address = write_address;
	g_power_fail_last_source = source;
	g_power_fail_state = POWER_FAIL_STATE_SAVING;
	g_power_fail_flash_operation_active = 1U;

	power_fail_record_build(&record, source);
	result = power_fail_flash_record_program(write_address, &record);

	g_power_fail_flash_operation_active = 0U;
	g_power_fail_last_result = result;

	if (result == POWER_FAIL_RESULT_OK)
	{
		g_power_fail_saved_count++;
		g_power_fail_record_available = 1U;
		g_power_fail_latest_sequence = record.sequence;
		s_latest_record_address = write_address;
		power_fail_record_copy_to_public((const volatile power_fail_record_t *)&record);
		power_fail_select_immediate_next_slot(write_address);
		g_power_fail_state = POWER_FAIL_STATE_SAVED;
	}
	else
	{
		g_power_fail_write_error_count++;
		g_power_fail_storage_ready = 0U;
		g_power_fail_next_write_address = 0U;
		g_power_fail_state = POWER_FAIL_STATE_ERROR;
	}

	return result;
}

static power_fail_result_t power_fail_event_latch(power_fail_source_t source)
{
	if (s_event_latched != 0U)
	{
		return POWER_FAIL_RESULT_ALREADY_LATCHED;
	}

	s_event_latched = 1U;
	power_fail_pending = 1U;
	power_fail_event_count++;
	g_power_fail_last_source = source;
	g_power_fail_state = POWER_FAIL_STATE_SAVING;

	return POWER_FAIL_RESULT_OK;
}

static power_fail_result_t power_fail_latched_event_save(power_fail_source_t source)
{
	power_fail_result_t result;

	result = power_fail_emergency_save(source);
	g_power_fail_last_result = result;

	if (result == POWER_FAIL_RESULT_STORAGE_BUSY)
	{
		s_save_retry_pending = 1U;
		s_save_retry_source = source;
	}

	if (result == POWER_FAIL_RESULT_NO_ERASED_SLOT)
	{
		g_power_fail_state = POWER_FAIL_STATE_NO_SPACE;
	}
	else if ((result != POWER_FAIL_RESULT_OK) && (result != POWER_FAIL_RESULT_STORAGE_BUSY))
	{
		g_power_fail_state = POWER_FAIL_STATE_ERROR;
	}

	return result;
}

static power_fail_result_t power_fail_event_latch_and_save(power_fail_source_t source)
{
	power_fail_result_t result;

	result = power_fail_event_latch(source);
	if (result != POWER_FAIL_RESULT_OK)
	{
		return result;
	}

	return power_fail_latched_event_save(source);
}

static power_fail_result_t power_fail_event_latch_deferred(power_fail_source_t source)
{
	power_fail_result_t result;

	result = power_fail_event_latch(source);
	if (result != POWER_FAIL_RESULT_OK)
	{
		return result;
	}

	s_deferred_save_source = source;
	g_power_fail_deferred_save_pending = 1U;

	if (g_power_fail_deferred_save_count != 0xFFFFFFFFU)
	{
		g_power_fail_deferred_save_count++;
	}

	return POWER_FAIL_RESULT_OK;
}

static void power_fail_deferred_save_process(void)
{
	uint32_t primask;
	uint8_t save_pending;
	power_fail_source_t source;
	power_fail_result_t result;

	primask = __get_PRIMASK();
	__disable_irq();

	save_pending = g_power_fail_deferred_save_pending;
	source = s_deferred_save_source;
	g_power_fail_deferred_save_pending = 0U;
	s_deferred_save_source = POWER_FAIL_SOURCE_NONE;

	if (primask == 0U)
	{
		__enable_irq();
	}

	if (save_pending == 0U)
	{
		return;
	}

	result = power_fail_latched_event_save(source);

	if ((result == POWER_FAIL_RESULT_OK) && (g_power_fail_deferred_save_complete_count != 0xFFFFFFFFU))
	{
		g_power_fail_deferred_save_complete_count++;
	}
}

power_fail_result_t power_fail_init(void)
{
	power_fail_result_t result;
	uint8_t current_pin_state;
	uint8_t allow_page_erase;
	uint32_t index;
	volatile uint32_t *snapshot_words = (volatile uint32_t *)&g_power_fail_snapshot;

	s_initialized = 0U;
	s_event_latched = 0U;
	s_inactive_timing = 0U;
	s_inactive_since_ms = 0U;
	s_latest_record_address = 0U;
	s_save_retry_pending = 0U;
	s_save_retry_source = POWER_FAIL_SOURCE_NONE;
	s_deferred_save_source = POWER_FAIL_SOURCE_NONE;

	power_fail_pending = 0U;
	power_fail_event_count = 0U;
	power_fail_active = 0U;
	power_fail_pin_state = 1U;
	power_fail_test_enable = 0U;
	power_fail_test_pin_state = 1U;

	g_power_fail_state = POWER_FAIL_STATE_UNINITIALIZED;
	g_power_fail_init_result = POWER_FAIL_RESULT_NOT_INITIALIZED;
	g_power_fail_last_result = POWER_FAIL_RESULT_NOT_INITIALIZED;
	g_power_fail_last_source = POWER_FAIL_SOURCE_NONE;
	g_power_fail_storage_ready = 0U;
	g_power_fail_record_available = 0U;
	g_power_fail_flash_operation_active = 0U;
	g_power_fail_deferred_save_pending = 0U;
	g_power_fail_test_request = 0U;
	g_power_fail_test_rearm_request = 0U;
	g_power_fail_next_write_address = 0U;
	g_power_fail_last_write_address = 0U;
	g_power_fail_latest_sequence = 0U;
	g_power_fail_irq_count = 0U;
	g_power_fail_deferred_save_count = 0U;
	g_power_fail_deferred_save_complete_count = 0U;
	g_power_fail_saved_count = 0U;
	g_power_fail_write_error_count = 0U;
	g_power_fail_crc_error_count = 0U;
	g_power_fail_invalid_record_count = 0U;
	g_power_fail_last_fmc_state = (uint32_t)FMC_READY;

	for (index = 0U; index < (sizeof(power_fail_snapshot_t) / sizeof(uint32_t)); index++)
	{
		snapshot_words[index] = 0U;
	}

	rcu_periph_clock_enable(POWER_FAIL_GPIO_RCU);
	rcu_periph_clock_enable(RCU_AF);

	gpio_init(POWER_FAIL_PORT, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, POWER_FAIL_PIN);
	gpio_exti_source_select(POWER_FAIL_PORT_SOURCE, POWER_FAIL_PIN_SOURCE);
	exti_init(POWER_FAIL_EXTI_LINE, EXTI_INTERRUPT, EXTI_TRIG_FALLING);
	exti_interrupt_flag_clear(POWER_FAIL_EXTI_LINE);

	current_pin_state = power_fail_hardware_pin_read();
	power_fail_pin_state = current_pin_state;
	power_fail_active = power_fail_active_from_pin(current_pin_state);
	s_previous_pin_state = current_pin_state;

	if (power_fail_active == 0U)
	{
		s_inactive_timing = 1U;
		s_inactive_since_ms = systick_get_ms();
	}

	g_power_fail_flash_kb = (uint32_t)(*(const volatile uint16_t *) POWER_FAIL_FLASH_DENSITY_ADDR);

	s_initialized = 1U;

	if (g_power_fail_flash_kb < POWER_FAIL_MINIMUM_FLASH_KB)
	{
		result = POWER_FAIL_RESULT_FLASH_SIZE_MISMATCH;
		g_power_fail_state = POWER_FAIL_STATE_ERROR;
	}
	else
	{
		allow_page_erase = (power_fail_active == 0U) ? 1U : 0U;
		result = power_fail_storage_prepare_internal(allow_page_erase);
	}

	g_power_fail_init_result = result;
	g_power_fail_last_result = result;

	NVIC_ClearPendingIRQ(POWER_FAIL_IRQ);
	NVIC_SetPriority(POWER_FAIL_IRQ, POWER_FAIL_IRQ_PRIORITY);
	NVIC_EnableIRQ(POWER_FAIL_IRQ);

	if (power_fail_active != 0U)
	{
		(void)power_fail_event_latch_and_save(POWER_FAIL_SOURCE_ACTIVE_AT_BOOT);
	}

	return result;
}

void power_fail_snapshot_update(const power_fail_snapshot_t *snapshot)
{
	volatile uint32_t *destination;
	const uint32_t *source;
	uint32_t index;
	uint32_t primask;

	if (snapshot == (const power_fail_snapshot_t *)0)
	{
		g_power_fail_last_result = POWER_FAIL_RESULT_INVALID_ARGUMENT;
		return;
	}

	destination = (volatile uint32_t *)&g_power_fail_snapshot;
	source = (const uint32_t *)snapshot;

	primask = __get_PRIMASK();
	__disable_irq();

	for (index = 0U; index < (sizeof(power_fail_snapshot_t) / sizeof(uint32_t)); index++)
	{
		destination[index] = source[index];
	}

	__DMB();

	if (primask == 0U)
	{
		__enable_irq();
	}
}

uint8_t power_fail_latest_record_get(power_fail_record_t *record)
{
	uint32_t *destination;
	const volatile uint32_t *source;
	uint32_t index;
	uint32_t primask;

	if (record == (power_fail_record_t *)0)
	{
		g_power_fail_last_result = POWER_FAIL_RESULT_INVALID_ARGUMENT;
		return 0U;
	}

	if (g_power_fail_record_available == 0U)
	{
		return 0U;
	}

	destination = (uint32_t *)record;
	source = (const volatile uint32_t *)&g_power_fail_latest_record;

	primask = __get_PRIMASK();
	__disable_irq();

	for (index = 0U; index < POWER_FAIL_WORDS_PER_RECORD; index++)
	{
		destination[index] = source[index];
	}

	if (primask == 0U)
	{
		__enable_irq();
	}

	return 1U;
}

uint8_t power_fail_pending_get(void)
{
	return power_fail_pending;
}

uint8_t power_fail_active_get(void)
{
	return power_fail_active;
}

void power_fail_clear(void)
{
	power_fail_pending = 0U;
}

void power_fail_rearm(void)
{
	uint8_t current_pin_state = power_fail_effective_pin_read();

	power_fail_pin_state = current_pin_state;
	power_fail_active = power_fail_active_from_pin(current_pin_state);

	if ((power_fail_active == 0U) && (g_power_fail_flash_operation_active == 0U) && (g_power_fail_deferred_save_pending == 0U))
	{
		s_event_latched = 0U;
		power_fail_pending = 0U;
		s_previous_pin_state = current_pin_state;
		s_inactive_timing = 1U;
		s_inactive_since_ms = systick_get_ms();

		if (g_power_fail_storage_ready != 0U)
		{
			g_power_fail_state = POWER_FAIL_STATE_READY;
			g_power_fail_last_result = POWER_FAIL_RESULT_OK;
		}
		else
		{
			g_power_fail_state = POWER_FAIL_STATE_NO_SPACE;
		}
	}
}

power_fail_result_t power_fail_storage_prepare(void)
{
	power_fail_result_t result;

	if (s_initialized == 0U)
	{
		return POWER_FAIL_RESULT_NOT_INITIALIZED;
	}

	if ((g_power_fail_flash_operation_active != 0U) || (g_power_fail_deferred_save_pending != 0U))
	{
		return POWER_FAIL_RESULT_STORAGE_BUSY;
	}

	if (power_fail_hardware_is_active() != 0U)
	{
		return POWER_FAIL_RESULT_POWER_UNSTABLE;
	}

	result = power_fail_storage_prepare_internal(1U);
	g_power_fail_last_result = result;
	return result;
}

power_fail_result_t power_fail_storage_format(void)
{
	power_fail_result_t result;

	if (s_initialized == 0U)
	{
		return POWER_FAIL_RESULT_NOT_INITIALIZED;
	}

	if ((g_power_fail_flash_operation_active != 0U) || (g_power_fail_deferred_save_pending != 0U))
	{
		return POWER_FAIL_RESULT_STORAGE_BUSY;
	}

	if (power_fail_hardware_is_active() != 0U)
	{
		return POWER_FAIL_RESULT_POWER_UNSTABLE;
	}

	g_power_fail_state = POWER_FAIL_STATE_PREPARING;
	g_power_fail_storage_ready = 0U;
	g_power_fail_next_write_address = 0U;
	g_power_fail_flash_operation_active = 1U;

	result = power_fail_flash_page_erase(POWER_FAIL_FLASH_PAGE_A_ADDR);

	if (result == POWER_FAIL_RESULT_OK)
	{
		result = power_fail_flash_page_erase(POWER_FAIL_FLASH_PAGE_B_ADDR);
	}

	g_power_fail_flash_operation_active = 0U;

	if (result == POWER_FAIL_RESULT_OK)
	{
		g_power_fail_record_available = 0U;
		g_power_fail_latest_sequence = 0U;
		s_latest_record_address = 0U;
		power_fail_record_public_clear();
		g_power_fail_next_write_address = POWER_FAIL_FLASH_PAGE_A_ADDR;
		g_power_fail_storage_ready = 1U;
		g_power_fail_state = POWER_FAIL_STATE_READY;
	}
	else
	{
		g_power_fail_state = POWER_FAIL_STATE_ERROR;
	}

	g_power_fail_last_result = result;
	return result;
}

power_fail_result_t power_fail_trigger_software_test(void)
{
#if (POWER_FAIL_ENABLE_SOFTWARE_TEST == 1U)
	power_fail_result_t result;

	if (s_initialized == 0U)
	{
		return POWER_FAIL_RESULT_NOT_INITIALIZED;
	}

	if (s_event_latched != 0U)
	{
		return POWER_FAIL_RESULT_ALREADY_LATCHED;
	}

	if (g_power_fail_storage_ready == 0U)
	{
		result = power_fail_storage_prepare();

		if (result != POWER_FAIL_RESULT_OK)
		{
			return result;
		}
	}

	return power_fail_event_latch_and_save(POWER_FAIL_SOURCE_SOFTWARE_TEST);
#else
	return POWER_FAIL_RESULT_TEST_DISABLED;
#endif
}

void power_fail_task(void)
{
	uint8_t current_pin_state;
	uint32_t current_time;
	uint32_t inactive_elapsed;
	uint8_t inactive_stable = 0U;
	power_fail_result_t result;

	if (s_initialized == 0U)
	{
		return;
	}

	power_fail_deferred_save_process();

	if (g_power_fail_test_request != 0U)
	{
		g_power_fail_test_request = 0U;
		(void)power_fail_trigger_software_test();
	}

	if (g_power_fail_test_rearm_request != 0U)
	{
		g_power_fail_test_rearm_request = 0U;
		power_fail_clear();
		power_fail_rearm();
	}

	current_pin_state = power_fail_effective_pin_read();
	power_fail_pin_state = current_pin_state;
	power_fail_active = power_fail_active_from_pin(current_pin_state);

	if ((s_previous_pin_state != POWER_FAIL_ACTIVE_LEVEL) && (current_pin_state == POWER_FAIL_ACTIVE_LEVEL))
	{
		(void)power_fail_event_latch_and_save((power_fail_test_enable != 0U) ? POWER_FAIL_SOURCE_SOFTWARE_TEST : POWER_FAIL_SOURCE_POLLING_FALLBACK);
	}

	current_time = systick_get_ms();

	if (power_fail_active != 0U)
	{
		s_inactive_timing = 0U;
	}
	else
	{
		if (s_inactive_timing == 0U)
		{
			s_inactive_timing = 1U;
			s_inactive_since_ms = current_time;
		}

		inactive_elapsed = (uint32_t)(current_time - s_inactive_since_ms);

		if (inactive_elapsed >= POWER_FAIL_REARM_STABLE_MS)
		{
			inactive_stable = 1U;
		}
	}

	if ((inactive_stable != 0U) && (power_fail_pending == 0U) && (s_event_latched != 0U))
	{
		power_fail_rearm();
	}

	if ((inactive_stable != 0U) && (power_fail_hardware_is_active() == 0U) && (g_power_fail_storage_ready == 0U) && (g_power_fail_flash_operation_active == 0U) && (g_power_fail_deferred_save_pending == 0U))
	{
		result = power_fail_storage_prepare_internal(1U);
		g_power_fail_last_result = result;

		if ((power_fail_pending != 0U) && (result == POWER_FAIL_RESULT_OK))
		{
			g_power_fail_state = POWER_FAIL_STATE_SAVED;
		}
	}

	s_previous_pin_state = current_pin_state;
}

void power_fail_update(void)
{
	power_fail_task();
}

void power_fail_shared_exti5_9_irq_handler(void)
{
	uint8_t current_pin_state;

	if (exti_interrupt_flag_get(POWER_FAIL_EXTI_LINE) != RESET)
	{
		exti_interrupt_flag_clear(POWER_FAIL_EXTI_LINE);
		g_power_fail_irq_count++;

		if (power_fail_test_enable == 0U)
		{
			current_pin_state = power_fail_hardware_pin_read();
			power_fail_pin_state = current_pin_state;
			power_fail_active = power_fail_active_from_pin(current_pin_state);
			s_previous_pin_state = current_pin_state;

			if (power_fail_active != 0U)
			{
				(void)power_fail_event_latch_deferred(POWER_FAIL_SOURCE_PB8_EXTI);
			}
		}
	}

#if (POWER_FAIL_FORWARD_WATCHDOG_IRQ == 1U)
	external_watchdog_wdo_irq_handler();
#endif
}

#if (POWER_FAIL_DEFINE_SHARED_IRQ_HANDLER == 1U)
void EXTI5_9_IRQHandler(void)
{
	power_fail_shared_exti5_9_irq_handler();
}
#endif
