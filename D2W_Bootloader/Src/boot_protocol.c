#include "boot_protocol.h"

#include "boot_config.h"
#include "boot_crc32.h"
#include "boot_flash.h"
#include "boot_image.h"
#include "boot_metadata.h"
#include "boot_time.h"
#include "boot_uart.h"
#include "boot_watchdog.h"

#define BOOT_FRAME_SOF_0  (0xA5U)
#define BOOT_FRAME_SOF_1  (0x5AU)
#define BOOT_FRAME_HEADER_SIZE  (6U)

#define BOOT_COMMAND_HELLO  (0x01U)
#define BOOT_COMMAND_BEGIN  (0x02U)
#define BOOT_COMMAND_DATA  (0x03U)
#define BOOT_COMMAND_END  (0x04U)
#define BOOT_COMMAND_ABORT  (0x05U)
#define BOOT_COMMAND_BOOT  (0x06U)
#define BOOT_COMMAND_QUERY  (0x07U)
#define BOOT_COMMAND_RESPONSE  (0x80U)

#define BOOT_RESPONSE_PAYLOAD_SIZE  (20U)

typedef enum
{
	BOOT_STATUS_OK = 0,
	BOOT_STATUS_BAD_FRAME,
	BOOT_STATUS_BAD_CRC,
	BOOT_STATUS_BAD_COMMAND,
	BOOT_STATUS_BAD_LENGTH,
	BOOT_STATUS_INVALID_STATE,
	BOOT_STATUS_INVALID_IMAGE,
	BOOT_STATUS_FLASH_ERROR,
	BOOT_STATUS_BAD_OFFSET,
	BOOT_STATUS_WRONG_BOARD,
	BOOT_STATUS_TIMEOUT,
	BOOT_STATUS_APPLICATION_INVALID
} boot_status_t;

typedef enum
{
	BOOT_RECEIVE_OK = 0,
	BOOT_RECEIVE_NO_PACKET,
	BOOT_RECEIVE_BAD_FRAME,
	BOOT_RECEIVE_BAD_CRC
} boot_receive_result_t;

typedef struct
{
	uint8_t command;
	uint16_t sequence;
	uint16_t payload_length;
	uint8_t payload[BOOT_PACKET_MAX_PAYLOAD];
} boot_packet_t;

volatile boot_protocol_state_t g_boot_protocol_state = BOOT_PROTOCOL_STATE_IDLE;
volatile uint32_t g_boot_protocol_next_offset = 0UL;
volatile uint32_t g_boot_protocol_expected_size = 0UL;
volatile uint32_t g_boot_protocol_expected_crc32 = 0UL;
volatile uint32_t g_boot_protocol_image_version = 0UL;
volatile uint32_t g_boot_protocol_valid_packet_count = 0UL;
volatile uint32_t g_boot_protocol_error_count = 0UL;

static uint32_t s_running_crc32 = 0xFFFFFFFFUL;

static uint16_t boot_read_u16_le(const uint8_t *data)
{
	return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static uint32_t boot_read_u32_le(const uint8_t *data)
{
	return ((uint32_t)data[0]) | ((uint32_t)data[1] << 8UL) | ((uint32_t)data[2] << 16UL) | ((uint32_t)data[3] << 24UL);
}

static void boot_write_u16_le(uint8_t *data, uint16_t value)
{
	data[0] = (uint8_t)(value & 0xFFU);
	data[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static void boot_write_u32_le(uint8_t *data, uint32_t value)
{
	data[0] = (uint8_t)(value & 0xFFUL);
	data[1] = (uint8_t)((value >> 8UL) & 0xFFUL);
	data[2] = (uint8_t)((value >> 16UL) & 0xFFUL);
	data[3] = (uint8_t)((value >> 24UL) & 0xFFUL);
}

static uint8_t boot_receive_exact(uint8_t *data, uint32_t length, uint32_t timeout_ms)
{
	uint32_t index;

	for (index = 0UL; index < length; index++)
	{
		if (boot_uart_receive_byte(&data[index], timeout_ms) !=	BOOT_UART_RESULT_OK)
		{
			return 0U;
		}
	}

	return 1U;
}

static boot_receive_result_t boot_receive_packet(boot_packet_t *packet, uint32_t start_timeout_ms)
{
	uint8_t data;
	uint8_t saw_first_sof = 0U;
	uint8_t header[BOOT_FRAME_HEADER_SIZE];
	uint8_t crc_bytes[4];
	uint32_t start_ms = boot_time_now();
	uint32_t calculated_crc;
	uint32_t received_crc;

	while (boot_time_elapsed(start_ms, start_timeout_ms) == 0U)
	{
		if (boot_uart_receive_byte(&data, 1UL) != BOOT_UART_RESULT_OK)
		{
			continue;
		}

		if (saw_first_sof == 0U)
		{
			if (data == BOOT_FRAME_SOF_0)
			{
				saw_first_sof = 1U;
			}

			continue;
		}

		if (data == BOOT_FRAME_SOF_1)
		{
			break;
		}

		saw_first_sof = (data == BOOT_FRAME_SOF_0) ? 1U : 0U;
	}

	if (saw_first_sof == 0U)
	{
		return BOOT_RECEIVE_NO_PACKET;
	}

	if (data != BOOT_FRAME_SOF_1)
	{
		return BOOT_RECEIVE_NO_PACKET;
	}

	if (boot_receive_exact(header, BOOT_FRAME_HEADER_SIZE, BOOT_PACKET_INTERBYTE_TIMEOUT_MS) == 0U)
	{
		return BOOT_RECEIVE_BAD_FRAME;
	}

	packet->command = header[1];
	packet->sequence = boot_read_u16_le(&header[2]);
	packet->payload_length = boot_read_u16_le(&header[4]);

	if ((header[0] != BOOT_PROTOCOL_VERSION) || (packet->payload_length > BOOT_PACKET_MAX_PAYLOAD))
	{
		return BOOT_RECEIVE_BAD_FRAME;
	}

	if (packet->payload_length > 0U)
	{
		if (boot_receive_exact(packet->payload, packet->payload_length, BOOT_PACKET_INTERBYTE_TIMEOUT_MS) == 0U)
		{
			return BOOT_RECEIVE_BAD_FRAME;
		}
	}

	if (boot_receive_exact(crc_bytes, sizeof(crc_bytes), BOOT_PACKET_INTERBYTE_TIMEOUT_MS) == 0U)
	{
		return BOOT_RECEIVE_BAD_FRAME;
	}

	calculated_crc = boot_crc32_begin();
	calculated_crc = boot_crc32_update(calculated_crc, header, sizeof(header));
	calculated_crc = boot_crc32_update(calculated_crc, packet->payload, packet->payload_length);
	calculated_crc = boot_crc32_finish(calculated_crc);
	received_crc = boot_read_u32_le(crc_bytes);

	if (calculated_crc != received_crc)
	{
		return BOOT_RECEIVE_BAD_CRC;
	}

	return BOOT_RECEIVE_OK;
}

static void boot_send_packet(uint8_t command, uint16_t sequence, const uint8_t *payload, uint16_t payload_length)
{
	uint8_t sof[2] = {BOOT_FRAME_SOF_0, BOOT_FRAME_SOF_1};
	uint8_t header[BOOT_FRAME_HEADER_SIZE];
	uint8_t crc_bytes[4];
	uint32_t crc;

	header[0] = BOOT_PROTOCOL_VERSION;
	header[1] = command;
	boot_write_u16_le(&header[2], sequence);
	boot_write_u16_le(&header[4], payload_length);

	crc = boot_crc32_begin();
	crc = boot_crc32_update(crc, header, sizeof(header));
	crc = boot_crc32_update(crc, payload, payload_length);
	crc = boot_crc32_finish(crc);
	boot_write_u32_le(crc_bytes, crc);

	boot_uart_send(sof, sizeof(sof));
	boot_uart_send(header, sizeof(header));

	if (payload_length > 0U)
	{
		boot_uart_send(payload, payload_length);
	}

	boot_uart_send(crc_bytes, sizeof(crc_bytes));
}

static void boot_send_response(uint8_t request_command, uint16_t sequence, boot_status_t status)
{
	uint8_t payload[BOOT_RESPONSE_PAYLOAD_SIZE];
	uint32_t installed_version = 0UL;

	if (g_boot_metadata_valid != 0U)
	{
		installed_version = g_boot_metadata_record.image_version;
	}

	payload[0] = request_command;
	payload[1] = (uint8_t)status;
	payload[2] = (uint8_t)g_boot_protocol_state;
	payload[3] = BOOT_PROTOCOL_VERSION;
	boot_write_u32_le(&payload[4], g_boot_protocol_next_offset);
	boot_write_u32_le(&payload[8], BOOT_APPLICATION_MAX_SIZE);
	boot_write_u32_le(&payload[12], (uint32_t)g_boot_active_application_valid);
	boot_write_u32_le(&payload[16], installed_version);

	boot_send_packet(BOOT_COMMAND_RESPONSE, sequence, payload, sizeof(payload));
}

static boot_status_t boot_process_begin(const boot_packet_t *packet)
{
	uint32_t board_id;
	uint32_t image_version;
	uint32_t image_size;
	uint32_t image_crc32;

	if (packet->payload_length != 16U)
	{
		return BOOT_STATUS_BAD_LENGTH;
	}

	board_id = boot_read_u32_le(&packet->payload[0]);
	image_version = boot_read_u32_le(&packet->payload[4]);
	image_size = boot_read_u32_le(&packet->payload[8]);
	image_crc32 = boot_read_u32_le(&packet->payload[12]);

	if (board_id != BOOT_BOARD_ID)
	{
		return BOOT_STATUS_WRONG_BOARD;
	}

	if ((image_size < 8UL) || (image_size > BOOT_APPLICATION_MAX_SIZE))
	{
		return BOOT_STATUS_INVALID_IMAGE;
	}

	if (boot_metadata_clear() == 0U)
	{
		return BOOT_STATUS_FLASH_ERROR;
	}

	if (boot_flash_erase_staging(image_size) != BOOT_FLASH_RESULT_OK)
	{
		return BOOT_STATUS_FLASH_ERROR;
	}

	g_boot_protocol_expected_size = image_size;
	g_boot_protocol_expected_crc32 = image_crc32;
	g_boot_protocol_image_version = image_version;
	g_boot_protocol_next_offset = 0UL;
	s_running_crc32 = boot_crc32_begin();
	g_boot_protocol_state = BOOT_PROTOCOL_STATE_RECEIVING;
	return BOOT_STATUS_OK;
}

static boot_status_t boot_process_data(const boot_packet_t *packet)
{
	uint32_t offset;
	uint32_t data_length;

	if (g_boot_protocol_state != BOOT_PROTOCOL_STATE_RECEIVING)
	{
		return BOOT_STATUS_INVALID_STATE;
	}

	if ((packet->payload_length < 5U) || (packet->payload_length > BOOT_PACKET_MAX_PAYLOAD))
	{
		return BOOT_STATUS_BAD_LENGTH;
	}

	offset = boot_read_u32_le(&packet->payload[0]);
	data_length = (uint32_t)packet->payload_length - 4UL;

	if ((offset != g_boot_protocol_next_offset) || ((offset & 3UL) != 0UL))
	{
		return BOOT_STATUS_BAD_OFFSET;
	}

	if ((offset + data_length < offset) || (offset + data_length > g_boot_protocol_expected_size))
	{
		return BOOT_STATUS_BAD_LENGTH;
	}

	if (((offset + data_length) < g_boot_protocol_expected_size) && ((data_length & 3UL) != 0UL))
	{
		return BOOT_STATUS_BAD_LENGTH;
	}

	if (boot_flash_program(BOOT_STAGING_ADDRESS + offset, &packet->payload[4], data_length) != BOOT_FLASH_RESULT_OK)
	{
		g_boot_protocol_state = BOOT_PROTOCOL_STATE_ERROR;
		return BOOT_STATUS_FLASH_ERROR;
	}

	s_running_crc32 = boot_crc32_update(s_running_crc32, &packet->payload[4], data_length);
	g_boot_protocol_next_offset += data_length;
	return BOOT_STATUS_OK;
}

static boot_status_t boot_process_end(const boot_packet_t *packet)
{
	uint32_t received_crc32;

	if (packet->payload_length != 0U)
	{
		return BOOT_STATUS_BAD_LENGTH;
	}

	if (g_boot_protocol_state != BOOT_PROTOCOL_STATE_RECEIVING)
	{
		return BOOT_STATUS_INVALID_STATE;
	}

	if (g_boot_protocol_next_offset != g_boot_protocol_expected_size)
	{
		return BOOT_STATUS_BAD_LENGTH;
	}

	received_crc32 = boot_crc32_finish(s_running_crc32);

	if ((received_crc32 != g_boot_protocol_expected_crc32) || (boot_flash_crc32(BOOT_STAGING_ADDRESS, g_boot_protocol_expected_size) != g_boot_protocol_expected_crc32))
	{
		g_boot_protocol_state = BOOT_PROTOCOL_STATE_ERROR;
		return BOOT_STATUS_INVALID_IMAGE;
	}

	if (boot_image_vector_is_valid(BOOT_STAGING_ADDRESS, BOOT_APPLICATION_ADDRESS, g_boot_protocol_expected_size) == 0U)
	{
		g_boot_protocol_state = BOOT_PROTOCOL_STATE_ERROR;
		return BOOT_STATUS_INVALID_IMAGE;
	}

	if (boot_metadata_write_ready(g_boot_protocol_expected_size, g_boot_protocol_expected_crc32, g_boot_protocol_image_version) == 0U)
	{
		g_boot_protocol_state = BOOT_PROTOCOL_STATE_ERROR;
		return BOOT_STATUS_FLASH_ERROR;
	}

	g_boot_protocol_state = BOOT_PROTOCOL_STATE_READY;
	return BOOT_STATUS_OK;
}

static boot_status_t boot_process_abort(void)
{
	if (boot_metadata_clear() == 0U)
	{
		return BOOT_STATUS_FLASH_ERROR;
	}

	g_boot_protocol_state = BOOT_PROTOCOL_STATE_IDLE;
	g_boot_protocol_next_offset = 0UL;
	g_boot_protocol_expected_size = 0UL;
	g_boot_protocol_expected_crc32 = 0UL;
	g_boot_protocol_image_version = 0UL;
	s_running_crc32 = boot_crc32_begin();
	return BOOT_STATUS_OK;
}

boot_protocol_result_t boot_protocol_run(uint8_t forced_mode, uint8_t application_valid)
{
	boot_packet_t packet;
	boot_receive_result_t receive_result;
	boot_status_t status;
	uint8_t session_active = 0U;
	uint32_t listen_start_ms = boot_time_now();
	uint32_t last_packet_ms = listen_start_ms;

	g_boot_protocol_state = BOOT_PROTOCOL_STATE_IDLE;
	g_boot_protocol_next_offset = 0UL;
	g_boot_protocol_expected_size = 0UL;
	g_boot_protocol_expected_crc32 = 0UL;
	g_boot_protocol_image_version = 0UL;

	while (1)
	{
		receive_result = boot_receive_packet(&packet, BOOT_PACKET_START_TIMEOUT_MS);

		if (receive_result == BOOT_RECEIVE_NO_PACKET)
		{
			if ((session_active == 0U) && (forced_mode == 0U) && (boot_time_elapsed(listen_start_ms, BOOT_NORMAL_LISTEN_WINDOW_MS) != 0U))
			{
				return BOOT_PROTOCOL_RUN_APPLICATION;
			}

			if ((session_active != 0U) && (forced_mode == 0U) && (boot_time_elapsed(last_packet_ms, BOOT_ACTIVE_SESSION_TIMEOUT_MS) != 0U))
			{
				return BOOT_PROTOCOL_RUN_APPLICATION;
			}

			continue;
		}

		last_packet_ms = boot_time_now();

		if (receive_result == BOOT_RECEIVE_BAD_CRC)
		{
			g_boot_protocol_error_count++;
			boot_send_response(packet.command, packet.sequence, BOOT_STATUS_BAD_CRC);
			continue;
		}

		if (receive_result != BOOT_RECEIVE_OK)
		{
			g_boot_protocol_error_count++;
			continue;
		}

		if (g_boot_protocol_valid_packet_count != 0xFFFFFFFFUL)
		{
			g_boot_protocol_valid_packet_count++;
		}

		session_active = 1U;
		status = BOOT_STATUS_OK;

		switch (packet.command)
		{
			case BOOT_COMMAND_HELLO:
			case BOOT_COMMAND_QUERY:
				if (packet.payload_length != 0U)
				{
					status = BOOT_STATUS_BAD_LENGTH;
				}
				break;

			case BOOT_COMMAND_BEGIN:
				status = boot_process_begin(&packet);
				break;

			case BOOT_COMMAND_DATA:
				status = boot_process_data(&packet);
				break;

			case BOOT_COMMAND_END:
				status = boot_process_end(&packet);
				break;

			case BOOT_COMMAND_ABORT:
				status = boot_process_abort();
				break;

			case BOOT_COMMAND_BOOT:
				if (packet.payload_length != 0U)
				{
					status = BOOT_STATUS_BAD_LENGTH;
				}
				else if (boot_image_current_active_is_valid() == 0U)
				{
					status = BOOT_STATUS_APPLICATION_INVALID;
				}
				break;

			default:
				status = BOOT_STATUS_BAD_COMMAND;
				break;
		}

		if (status != BOOT_STATUS_OK)
		{
			if (g_boot_protocol_error_count != 0xFFFFFFFFUL)
			{
				g_boot_protocol_error_count++;
			}
		}

		application_valid = boot_image_current_active_is_valid();
		boot_send_response(packet.command, packet.sequence, status);
		boot_uart_wait_transmission_complete();

		if ((packet.command == BOOT_COMMAND_END) && (status == BOOT_STATUS_OK))
		{
			boot_time_delay_ms(20UL);
			NVIC_SystemReset();
		}

		if ((packet.command == BOOT_COMMAND_BOOT) && (status == BOOT_STATUS_OK) && (application_valid != 0U))
		{
			return BOOT_PROTOCOL_RUN_APPLICATION;
		}
	}
}
