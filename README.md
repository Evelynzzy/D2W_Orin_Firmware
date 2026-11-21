# GD32F303 Orin Carrier Firmware

Firmware for a custom NVIDIA Jetson Orin NX carrier board, implemented as two independent Keil projects:

- a bare-metal Bootloader for image validation, recovery, and UART firmware updates;
- a FreeRTOS Application for power sequencing, supervision, monitoring, and fault handling.

The target MCU is the GD32F303VCT6 (Arm Cortex-M4F, 256 KiB internal Flash, 48 KiB SRAM).

## Features

### Bootloader

- Validates the Application vector table and CRC32 before execution.
- Receives firmware images over UART at 115200 8N1.
- Writes new images to a dedicated staging region before installation.
- Uses persistent metadata to recover from an interrupted copy operation.
- Services the external watchdog while waiting for an update.
- Relocates the vector table and transfers control to the Application.
- Includes a Python host-side update utility.

### Application

- Runs seven statically allocated FreeRTOS tasks.
- Controls the Orin main supply, `POWER_EN`, `SYS_RESET*`, and carrier-board power rails.
- Handles active-low `SHUTDOWN_REQ*` and performs an orderly shutdown sequence.
- Feeds and monitors an external hardware watchdog.
- Samples the Orin input, 5 V, 3.3 V, and 1.8 V rails with ADC0.
- Reads and writes an external HYM8563S real-time clock over I2C1.
- Detects power-fail events and stores a compact record in internal Flash.
- Publishes task health, fault states, stack margins, and diagnostic counters.
- Uses static allocation only; no FreeRTOS heap implementation is linked.

GNSS, PPS, LiDAR, and time-distribution pin definitions are reserved in the board support header, but their runtime modules are not included in the current Application build.

## Repository Layout

```text
.
├── Bootloader/
│   ├── D2W_Bootloader.uvprojx
│   ├── Common/
│   ├── Host/
│   ├── Inc/
│   ├── Libraries/
│   └── Src/
├── Application/
│   ├── D2W_Application.uvprojx
│   ├── App/
│   ├── BSP/
│   ├── Common/
│   ├── Config/
│   ├── Libraries/
│   ├── Middlewares/
│   ├── Tools/
│   └── User/
├── .gitignore
└── README.md
```

The two project directories are independent. Each carries the startup code, CMSIS files, and GD32 peripheral-library sources required by that image.

## Flash and RAM Layout

Both projects share the memory map in `Common/boot_shared.h`.

| Region | Start | End | Size |
|---|---:|---:|---:|
| Bootloader | `0x08000000` | `0x08007FFF` | `0x00008000` |
| Active Application | `0x08008000` | `0x08022FFF` | `0x0001B000` |
| Update staging | `0x08023000` | `0x0803DFFF` | `0x0001B000` |
| Update metadata | `0x0803E000` | `0x0803EFFF` | `0x00001000` |
| Power-fail records | `0x0803F000` | `0x0803FFFF` | `0x00001000` |

Both images use SRAM from `0x20000000` with a configured size of `0x0000C000`.

The Application must remain linked at `0x08008000` and must set `SCB->VTOR` to that address before enabling interrupts or starting FreeRTOS. Linking it at `0x08000000` would overwrite the Bootloader.

## Application Architecture

| Task | Priority | Main responsibility |
|---|---:|---|
| Storage | 6 | Owns Flash access and persists power-fail records |
| Watchdog | 5 | Generates WDI edges and handles WDO faults |
| PowerManager | 4 | Serializes power, reset, and shutdown operations |
| OrinStatus | 3 | Debounces and interprets the Orin reset-status input |
| Voltage | 2 | Samples ADC0 channels and evaluates rail faults |
| RTC | 2 | Owns I2C1 and the external calendar RTC |
| Diagnostics | 1 | Captures health counters, event bits, and stack margins |

The tasks communicate through one power-command queue, one event group, direct-to-task notifications, and a diagnostics software timer. Hardware ownership is deliberately exclusive: only one task writes Flash, controls power GPIOs, accesses ADC0, accesses I2C1, or toggles the watchdog input.

All task control blocks, stacks, queues, event groups, and timers are statically allocated. Do not add a `heap_*.c` file unless the memory model is intentionally redesigned.

## Boot Flow

1. The Bootloader initializes its time base, external watchdog service, and update UART.
2. It reads the update request and persistent metadata.
3. A valid pending image is checked and installed from staging when required.
4. The active image's vector table, address range, size, and CRC are validated.
5. During a normal boot, the UART update listener remains available for a short entry window.
6. Before the handoff, interrupts and SysTick are disabled, pending NVIC state is cleared, and `SCB->VTOR` is moved to `0x08008000`.
7. The corrected handoff bridge loads the Application MSP and branches directly to its reset handler without executing compiled Bootloader C code on the replaced stack.
8. The Application creates the static FreeRTOS objects, starts the scheduler, initializes safe GPIO states, and performs the controlled Orin power-up sequence.

## Key MCU Interfaces

The authoritative mapping is `Application/BSP/board_pins.h`. The following pins are used by the current firmware:

| MCU pin | Function | Active behavior |
|---|---|---|
| PB14 | Orin main input-supply enable | High enables the supply |
| PA7 | Orin `POWER_EN` | High enables startup |
| PB15 | Orin `SYS_RESET*` control | Open-drain low asserts reset; released is normal |
| PA8 | Orin `SYS_RESET*` feedback | Low means reset is asserted; high means released |
| PA11 | Orin `SHUTDOWN_REQ*` | Active low, falling-edge interrupt |
| PB6 | External watchdog WDI | Periodically toggled by firmware |
| PB7 | External watchdog WDO# | Active-low timeout indication |
| PB8 | Power-fail indication | Active-low, falling-edge interrupt |
| PB10 / PB11 | HYM8563S RTC | I2C1 SCL / SDA |
| PC1 / PC2 / PC3 / PC5 | Voltage sensing | ADC0 inputs for Orin, 5 V, 3.3 V, and 1.8 V |
| PA0 / PA1 / PA4 / PC8 | Carrier peripheral rails | High enables the corresponding rail |
| PC10 / PC11 | Bootloader update UART | TX / RX, 115200 8N1 |

> **Reset mapping:** PB15 is the reset-control output and PA8 is the reset-status input. Do not swap these roles.

## Orin Power Sequence

The normal startup path performs the following operations:

1. Initialize all controlled outputs to their safe states.
2. Wait approximately 100 ms after Application startup.
3. Assert PB14 to enable the Orin input supply.
4. Wait 75 ms, release PB15, and assert PA7.
5. Enable the carrier peripheral rails.
6. Wait up to 1000 ms for PA8 to report that `SYS_RESET*` has been released.
7. Enter the `ON` state and enable normal shutdown-request processing.

If PA8 remains low, the normal build reports `POWER_CTRL_RESULT_SYS_RESET_RELEASE_TIMEOUT`, enters a fault state, and disables the controlled rails. This behavior is expected when the Orin module is absent.

## Production and Carrier-Only Test Modes

For normal operation with an Orin module installed, keep:

```c
#define POWER_CTRL_CARRIER_ONLY_TEST_MODE 0U
```

It may also be omitted from Keil's **C/C++ > Preprocessor Symbols > Define** field because `power_ctrl.h` defaults it to `0U`.

For a temporary carrier-only bench test without an Orin module, it can be overridden to `1U`. That setting bypasses only the missing PA8 reset-release feedback so the remaining carrier rails can stay enabled.

```text
GD32F30X_HD,POWER_CTRL_CARRIER_ONLY_TEST_MODE=1U
```

Carrier-only mode is not a production configuration. Set it back to `0U` or remove the override before testing or releasing firmware with an Orin module installed.

## Toolchain

- Keil MDK 5
- Arm Compiler 6.24
- GigaDevice GD32F30x Device Family Pack 2.2.3
- GD32F30x Firmware Library V2.1.5
- FreeRTOS Kernel V11.1.0
- ST-Link with SWD
- Python 3 and `pyserial` for UART updates

The vendor library and FreeRTOS sources used by the projects are included in the repository. Keep their original license files and notices when redistributing the source.

## Build

### Bootloader

1. Open `Bootloader/D2W_Bootloader.uvprojx`.
2. Confirm the target device is `GD32F303VC` and Arm Compiler 6 is selected.
3. Confirm IROM1 starts at `0x08000000` with size `0x00008000`.
4. Select **Rebuild** and require `0 Error(s)`.
5. The generated files are placed in `Bootloader/Objects/`.

### Application

1. Open `Application/D2W_Application.uvprojx`.
2. Confirm IROM1 starts at `0x08008000` with size `0x0001B000`.
3. Confirm the `GD32F30X_HD` preprocessor symbol is present.
4. For an Orin-equipped release, confirm carrier-only test mode is not enabled.
5. Select **Rebuild** and require `0 Error(s)`.
6. The generated files are placed in `Application/Objects/`.

CMSIS compatibility warnings may appear with Arm Compiler 6, but compiler errors must not be ignored. In particular, preserve `configPRIO_BITS` and the FreeRTOS interrupt-priority definitions in `Config/FreeRTOSConfig.h`.

## Initial Programming

Use the GD32F30x high-density Flash algorithm and select:

- **Erase Sectors**
- **Program**
- **Verify**
- **Reset and Run**

Program the images in this order:

1. `Bootloader/Objects/D2W_Bootloader.hex`
2. `Application/Objects/D2W_Application.hex`
3. Fully remove power, restore power, and verify automatic Bootloader-to-Application startup.

> **Do not use Erase Full Chip after both images have been installed.** It also erases the other image, update metadata, and power-fail records. Use sector erase for normal firmware updates.

## UART Firmware Update

Install the host dependency:

```bash
python -m pip install pyserial
```

If `D2W_Application.bin` is not generated automatically, enable the Application project's post-build `fromelf` command or run the equivalent command from a Keil command prompt:

```bat
fromelf.exe --bin --output ".\Objects\D2W_Application.bin" ".\Objects\D2W_Application.axf"
```

Example update command:

```bash
python Bootloader/Host/d2w_uart_updater.py --port COM6 update Application/Objects/D2W_Application.bin --version 0x00010001
```

Replace `COM6` and the version value as required. The updater writes the image to staging, verifies it, commits metadata, and requests installation on the next boot.

CRC32 provides transfer-integrity checking, not authentication. Before production deployment, review the normal-boot UART entry policy and add an authenticated update mechanism if the threat model requires it.

## Validation Checklist

Before creating a release:

- Rebuild both projects with zero errors.
- Verify that the Bootloader and Application memory regions do not overlap.
- Confirm that the repository contains the corrected non-returning Bootloader handoff implementation.
- Perform at least 20 complete power-off cold starts.
- Confirm PB14, PA7, PB15, and PA8 timing on the target hardware.
- Confirm normal Orin startup and operating-system availability.
- Test the PA11 orderly-shutdown path and forced-power-off timeout.
- Test external-watchdog timeout detection and recovery.
- Check all four ADC readings and their fault/recovery thresholds.
- Verify RTC read, write, rollover, and backup retention.
- Verify UART update, CRC rejection, and interrupted-copy recovery.
- Test PB8 using a real power-loss event, not only a software-triggered event.
- Check the high-water mark of every FreeRTOS task stack.

The PB8 save path depends on the carrier board's hold-up energy. A software test verifies the queue, task, and Flash logic, but only a real power interruption verifies that enough time remains to complete the write.

## Debugging Notes

- Entering a Keil debug session can reset or halt the MCU, stop the watchdog feed, or interrupt the power state machine. A power-down observed only when attaching the debugger is not automatically a normal cold-start failure.
- When loading symbols without programming Flash, use `LOAD <path-to-axf> NOCODE INCREMENTAL` and confirm that the AXF path exists.
- Application variables cannot be evaluated while the PC is executing inside the Bootloader unless Application symbols have been loaded.
- The first two words at `0x08008000` must contain a valid SRAM initial MSP and an odd Thumb reset-handler address within the Application region.
