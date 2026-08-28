#!/usr/bin/env python3

import argparse
import struct
import sys
import time
import zlib

try:
    import serial
except ImportError:
    print("pyserial is required: python3 -m pip install pyserial")
    raise SystemExit(2)


SOF = b"\xA5\x5A"
PROTOCOL_VERSION = 1
BOARD_ID = 0x44325731
APPLICATION_ADDRESS = 0x08008000
APPLICATION_MAX_SIZE = 0x0001B000
SRAM_START = 0x20000000
SRAM_END = 0x2000C000

CMD_HELLO = 0x01
CMD_BEGIN = 0x02
CMD_DATA = 0x03
CMD_END = 0x04
CMD_ABORT = 0x05
CMD_BOOT = 0x06
CMD_QUERY = 0x07
CMD_RESPONSE = 0x80

STATUS_NAMES = {
    0: "OK",
    1: "BAD_FRAME",
    2: "BAD_CRC",
    3: "BAD_COMMAND",
    4: "BAD_LENGTH",
    5: "INVALID_STATE",
    6: "INVALID_IMAGE",
    7: "FLASH_ERROR",
    8: "BAD_OFFSET",
    9: "WRONG_BOARD",
    10: "TIMEOUT",
    11: "APPLICATION_INVALID",
}


class ProtocolError(RuntimeError):
    pass


class BootResponse:
    def __init__(self, payload):
        if len(payload) != 20:
            raise ProtocolError("response payload length is not 20 bytes")

        values = struct.unpack("<BBBBIIII", payload)
        self.request_command = values[0]
        self.status = values[1]
        self.state = values[2]
        self.protocol_version = values[3]
        self.next_offset = values[4]
        self.max_image_size = values[5]
        self.application_valid = values[6]
        self.installed_version = values[7]

    def describe(self):
        return (
            f"status={STATUS_NAMES.get(self.status, self.status)} "
            f"state={self.state} next_offset=0x{self.next_offset:08X} "
            f"max_size={self.max_image_size} "
            f"app_valid={self.application_valid} "
            f"installed_version=0x{self.installed_version:08X}"
        )


class BootTransport:
    def __init__(self, port, baudrate):
        self.serial = serial.Serial(
            port=port,
            baudrate=baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.05,
            write_timeout=2.0,
        )
        self.serial.dtr = False
        self.serial.rts = False
        self.sequence = 0

    def close(self):
        self.serial.close()

    def next_sequence(self):
        self.sequence = (self.sequence + 1) & 0xFFFF
        return self.sequence

    @staticmethod
    def build_frame(command, sequence, payload=b""):
        if len(payload) > 260:
            raise ProtocolError("payload exceeds the target limit")

        header = struct.pack(
            "<BBHH",
            PROTOCOL_VERSION,
            command,
            sequence,
            len(payload),
        )
        crc = zlib.crc32(header + payload) & 0xFFFFFFFF
        return SOF + header + payload + struct.pack("<I", crc)

    def write_frame(self, command, sequence, payload=b""):
        frame = self.build_frame(command, sequence, payload)
        self.serial.write(frame)
        self.serial.flush()

    def read_exact(self, length, deadline):
        data = bytearray()

        while len(data) < length:
            if time.monotonic() >= deadline:
                raise TimeoutError("serial response timeout")

            block = self.serial.read(length - len(data))
            if block:
                data.extend(block)

        return bytes(data)

    def read_frame(self, timeout):
        deadline = time.monotonic() + timeout
        saw_a5 = False

        while time.monotonic() < deadline:
            value = self.serial.read(1)
            if not value:
                continue

            byte = value[0]

            if not saw_a5:
                saw_a5 = byte == 0xA5
                continue

            if byte == 0x5A:
                break

            saw_a5 = byte == 0xA5
        else:
            raise TimeoutError("frame start timeout")

        header = self.read_exact(6, deadline)
        version, command, sequence, payload_length = struct.unpack(
            "<BBHH", header
        )

        if version != PROTOCOL_VERSION:
            raise ProtocolError(f"unsupported protocol version {version}")

        if payload_length > 260:
            raise ProtocolError("target returned an oversized payload")

        payload = self.read_exact(payload_length, deadline)
        received_crc = struct.unpack("<I", self.read_exact(4, deadline))[0]
        expected_crc = zlib.crc32(header + payload) & 0xFFFFFFFF

        if received_crc != expected_crc:
            raise ProtocolError("response CRC mismatch")

        return command, sequence, payload

    def exchange(self, command, payload=b"", timeout=3.0, retries=3):
        sequence = self.next_sequence()
        last_error = None

        for _ in range(retries):
            self.write_frame(command, sequence, payload)

            try:
                while True:
                    response_command, response_sequence, response_payload = (
                        self.read_frame(timeout)
                    )

                    if response_command != CMD_RESPONSE:
                        continue

                    if response_sequence != sequence:
                        continue

                    response = BootResponse(response_payload)

                    if response.request_command != command:
                        continue

                    return response
            except (TimeoutError, ProtocolError) as error:
                last_error = error

        raise ProtocolError(f"no valid response: {last_error}")


def parse_integer(value):
    return int(value, 0)


def validate_binary(image):
    if len(image) < 8:
        raise ProtocolError("binary is too small to contain a vector table")

    if len(image) > APPLICATION_MAX_SIZE:
        raise ProtocolError(
            f"binary is {len(image)} bytes but the slot limit is "
            f"{APPLICATION_MAX_SIZE} bytes"
        )

    initial_msp, reset_handler = struct.unpack_from("<II", image, 0)
    reset_address = reset_handler & ~1

    if not (SRAM_START <= initial_msp <= SRAM_END):
        raise ProtocolError(
            f"initial MSP 0x{initial_msp:08X} is outside GD32 SRAM"
        )

    if initial_msp & 0x7:
        raise ProtocolError("initial MSP is not 8 byte aligned")

    if (reset_handler & 1) == 0:
        raise ProtocolError("Reset_Handler does not have the Thumb bit set")

    if not (
        APPLICATION_ADDRESS
        <= reset_address
        < APPLICATION_ADDRESS + len(image)
    ):
        raise ProtocolError(
            "Reset_Handler is outside the linked Application image. "
            "Set Keil IROM1 Start to 0x08008000 before generating the binary."
        )


def require_ok(response, context):
    if response.status != 0:
        name = STATUS_NAMES.get(response.status, str(response.status))
        raise ProtocolError(
            f"{context} failed with {name}; {response.describe()}"
        )


def wait_for_bootloader(transport, timeout):
    print("Waiting for Bootloader. Reset or power cycle the MCU now.")
    deadline = time.monotonic() + timeout

    while time.monotonic() < deadline:
        try:
            response = transport.exchange(
                CMD_HELLO,
                timeout=0.35,
                retries=1,
            )
            require_ok(response, "HELLO")
            print("Bootloader connected:", response.describe())
            return response
        except (ProtocolError, TimeoutError):
            time.sleep(0.05)

    raise ProtocolError(
        "Bootloader was not detected. Check PC10 TX, PC11 RX, GND, "
        "115200 8N1 and reset timing."
    )


def run_query(args):
    transport = BootTransport(args.port, args.baud)

    try:
        wait_for_bootloader(transport, args.connect_timeout)

        response = transport.exchange(CMD_QUERY)
        require_ok(response, "QUERY")
        print(response.describe())

        response = transport.exchange(CMD_BOOT)
        require_ok(response, "BOOT")
        print("Boot command accepted. Application is starting.")
    finally:
        transport.close()


def send_data_block(transport, offset, block):
    payload = struct.pack("<I", offset) + block
    sequence = transport.next_sequence()
    last_error = None

    for _ in range(4):
        transport.write_frame(CMD_DATA, sequence, payload)

        try:
            while True:
                command, response_sequence, response_payload = (
                    transport.read_frame(3.0)
                )

                if command != CMD_RESPONSE or response_sequence != sequence:
                    continue

                response = BootResponse(response_payload)

                if response.request_command != CMD_DATA:
                    continue

                expected_next = offset + len(block)

                if response.status == 0:
                    if response.next_offset != expected_next:
                        raise ProtocolError(
                            "target ACK contains an unexpected next offset"
                        )
                    return response

                if (
                    response.status == 8
                    and response.next_offset == expected_next
                ):
                    return response

                require_ok(response, "DATA")
        except (TimeoutError, ProtocolError) as error:
            last_error = error

    raise ProtocolError(
        f"DATA at offset 0x{offset:08X} was not acknowledged: {last_error}"
    )


def run_update(args):
    with open(args.image, "rb") as image_file:
        image = image_file.read()

    validate_binary(image)
    image_crc32 = zlib.crc32(image) & 0xFFFFFFFF
    print(
        f"Image size={len(image)} bytes "
        f"CRC32=0x{image_crc32:08X} "
        f"version=0x{args.version:08X}"
    )

    transport = BootTransport(args.port, args.baud)

    try:
        hello = wait_for_bootloader(transport, args.connect_timeout)

        if len(image) > hello.max_image_size:
            raise ProtocolError("image exceeds the target reported slot size")

        begin_payload = struct.pack(
            "<IIII",
            BOARD_ID,
            args.version,
            len(image),
            image_crc32,
        )
        response = transport.exchange(
            CMD_BEGIN,
            begin_payload,
            timeout=20.0,
            retries=2,
        )
        require_ok(response, "BEGIN")
        print("Staging area erased")

        offset = 0

        while offset < len(image):
            block = image[offset : offset + 256]
            response = send_data_block(transport, offset, block)
            offset += len(block)
            percentage = (offset * 100) // len(image)
            print(
                f"\rTransferred {offset}/{len(image)} bytes "
                f"{percentage:3d}%",
                end="",
                flush=True,
            )

        print()
        response = transport.exchange(
            CMD_END,
            timeout=20.0,
            retries=1,
        )
        require_ok(response, "END")
        print("Image verified and committed in staging Flash")
        print("The MCU is resetting and will install the image automatically")
    finally:
        transport.close()


def build_parser():
    parser = argparse.ArgumentParser(
        description="D2 W GD32F303 UART3 internal Flash updater"
    )
    parser.add_argument("--port", required=True, help="COM6 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--connect-timeout", type=float, default=20.0)

    subparsers = parser.add_subparsers(dest="command", required=True)

    query_parser = subparsers.add_parser("query")
    query_parser.set_defaults(function=run_query)

    update_parser = subparsers.add_parser("update")
    update_parser.add_argument("image", help="Application raw binary file")
    update_parser.add_argument(
        "--version",
        required=True,
        type=parse_integer,
        help="monotonic firmware version such as 0x00010002",
    )
    update_parser.set_defaults(function=run_update)
    return parser


def main():
    parser = build_parser()
    args = parser.parse_args()

    try:
        args.function(args)
    except (OSError, serial.SerialException, ProtocolError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
