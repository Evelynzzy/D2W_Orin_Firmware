#!/usr/bin/env python3
"""Verify the D2W formal FreeRTOS Application project invariants.

This script checks project structure and source-level configuration. It does
not replace an ARMClang build, Flash programming verification, or board test.
"""

from __future__ import annotations

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ERRORS: list[str] = []
PASSES: list[str] = []


def pass_check(message: str) -> None:
    PASSES.append(message)


def fail_check(message: str) -> None:
    ERRORS.append(message)


def read_text(relative: str) -> str:
    path = ROOT / relative
    if not path.is_file():
        fail_check(f"missing file: {relative}")
        return ""
    return path.read_text(encoding="utf-8", errors="strict")


def require_text(text: str, token: str, context: str) -> None:
    if token in text:
        pass_check(f"{context}: {token}")
    else:
        fail_check(f"{context} missing: {token}")


def forbid_text(text: str, token: str, context: str) -> None:
    if token in text:
        fail_check(f"{context} contains forbidden token: {token}")
    else:
        pass_check(f"{context} excludes: {token}")


def require_order(text: str, tokens: list[str], context: str) -> None:
    positions = [text.find(token) for token in tokens]
    if all(position >= 0 for position in positions) and positions == sorted(positions):
        pass_check(f"{context}: order verified")
    else:
        fail_check(f"{context}: wrong or missing order: {tokens}")


def find_xml_text(root: ET.Element, path: str) -> str:
    element = root.find(path)
    return "" if element is None or element.text is None else element.text.strip()


def verify_required_files() -> None:
    required = [
        "D2W_Application.uvprojx",
        "User/main.c",
        "BSP/board_pins.h",
        "BSP/gd32f30x_libopt.h",
        "Common/boot_shared.h",
        "Config/FreeRTOSConfig.h",
        "App/system_tasks.c",
        "App/system_tasks.h",
        "App/power_ctrl.c",
        "App/power_ctrl.h",
        "App/watchdog_ctrl.c",
        "App/shutdown_request.c",
        "App/power_fail.c",
        "App/orin_status.c",
        "App/voltage_monitor.c",
        "App/rtc_calendar.c",
        "Middlewares/FreeRTOS-Kernel/tasks.c",
        "Middlewares/FreeRTOS-Kernel/queue.c",
        "Middlewares/FreeRTOS-Kernel/event_groups.c",
        "Middlewares/FreeRTOS-Kernel/timers.c",
        "Middlewares/FreeRTOS-Kernel/portable/GCC/ARM_CM4F/port.c",
    ]
    missing = [relative for relative in required if not (ROOT / relative).is_file()]
    if missing:
        for relative in missing:
            fail_check(f"missing required file: {relative}")
    else:
        pass_check(f"required files present: {len(required)}")


def verify_project() -> None:
    project_path = ROOT / "D2W_Application.uvprojx"
    try:
        xml_root = ET.parse(project_path).getroot()
    except (ET.ParseError, OSError) as error:
        fail_check(f"project XML parse failed: {error}")
        return

    device = find_xml_text(xml_root, ".//TargetOption/TargetCommonOption/Device")
    if device == "GD32F303VC":
        pass_check("target device: GD32F303VC")
    else:
        fail_check(f"unexpected target device: {device!r}")

    irom_start = find_xml_text(
        xml_root,
        ".//TargetArmAds/ArmAdsMisc/OnChipMemories/IROM/StartAddress",
    )
    irom_size = find_xml_text(
        xml_root,
        ".//TargetArmAds/ArmAdsMisc/OnChipMemories/IROM/Size",
    )
    iram_start = find_xml_text(
        xml_root,
        ".//TargetArmAds/ArmAdsMisc/OnChipMemories/IRAM/StartAddress",
    )
    iram_size = find_xml_text(
        xml_root,
        ".//TargetArmAds/ArmAdsMisc/OnChipMemories/IRAM/Size",
    )

    memory_values = {
        "IROM start": (irom_start.lower(), "0x08008000"),
        "IROM size": (irom_size.lower(), "0x0001b000"),
        "IRAM start": (iram_start.lower(), "0x20000000"),
        "IRAM size": (iram_size.lower(), "0xc000"),
    }
    for name, (actual, expected) in memory_values.items():
        if int(actual or "0", 16) == int(expected, 16):
            pass_check(f"{name}: {actual}")
        else:
            fail_check(f"{name}: expected {expected}, got {actual!r}")

    file_paths = [
        (element.text or "").replace("\\", "/").lower()
        for element in xml_root.findall(".//FilePath")
    ]
    joined_paths = "\n".join(file_paths)
    for forbidden in ("gnss", "pps_router", "time_sync", "airy", "lidar"):
        forbid_text(joined_paths, forbidden, "project source list")
    forbid_text(joined_paths, "gd32f30x_usart.c", "project source list")
    forbid_text(joined_paths, "heap_", "project source list")

    required_project_sources = [
        "./user/main.c",
        "./app/system_tasks.c",
        "./app/power_ctrl.c",
        "./app/watchdog_ctrl.c",
        "./app/shutdown_request.c",
        "./app/power_fail.c",
        "./app/orin_status.c",
        "./app/voltage_monitor.c",
        "./app/rtc_calendar.c",
        "./middlewares/freertos-kernel/tasks.c",
        "./middlewares/freertos-kernel/queue.c",
        "./middlewares/freertos-kernel/event_groups.c",
        "./middlewares/freertos-kernel/timers.c",
        "./middlewares/freertos-kernel/portable/gcc/arm_cm4f/port.c",
    ]
    for source in required_project_sources:
        if source in file_paths:
            pass_check(f"project source present: {source}")
        else:
            fail_check(f"project source missing: {source}")

    after_build_enabled = [
        (element.text or "").strip()
        for element in xml_root.findall(".//AfterMake/RunUserProg1")
    ]
    if after_build_enabled and all(value == "0" for value in after_build_enabled):
        pass_check("After Build command disabled by default")
    else:
        fail_check(f"After Build RunUserProg1 must be 0: {after_build_enabled}")


def verify_flash_layout() -> None:
    boot = read_text("Common/boot_shared.h")
    expected = {
        "BOOT_BOOTLOADER_ADDRESS": "0x08000000UL",
        "BOOT_BOOTLOADER_SIZE": "0x00008000UL",
        "BOOT_APPLICATION_ADDRESS": "0x08008000UL",
        "BOOT_APPLICATION_MAX_SIZE": "0x0001B000UL",
        "BOOT_STAGING_ADDRESS": "0x08023000UL",
        "BOOT_STAGING_SIZE": "0x0001B000UL",
        "BOOT_METADATA_ADDRESS": "0x0803E000UL",
        "BOOT_POWER_FAIL_ADDRESS": "0x0803F000UL",
    }
    for macro, value in expected.items():
        pattern = rf"#define\s+{macro}\s+\({re.escape(value)}\)"
        if re.search(pattern, boot):
            pass_check(f"Flash macro: {macro}={value}")
        else:
            fail_check(f"Flash macro mismatch: {macro}={value}")


def verify_freertos_config() -> None:
    config = read_text("Config/FreeRTOSConfig.h")
    requirements = [
        "#define configMAX_PRIORITIES                    (7U)",
        "#define configUSE_TIME_SLICING                  (0)",
        "#define configUSE_TASK_NOTIFICATIONS            (1)",
        "#define configUSE_MUTEXES                       (0)",
        "#define configUSE_TIMERS                        (1)",
        "#define configUSE_EVENT_GROUPS                  (1)",
        "#define configSUPPORT_STATIC_ALLOCATION         (1)",
        "#define configSUPPORT_DYNAMIC_ALLOCATION        (0)",
        "#define configCHECK_FOR_STACK_OVERFLOW          (2)",
        "#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5U)",
        "#define vPortSVCHandler                         SVC_Handler",
        "#define xPortPendSVHandler                      PendSV_Handler",
        "#define xPortSysTickHandler                     SysTick_Handler",
    ]
    for requirement in requirements:
        require_text(config, requirement, "FreeRTOSConfig")


def verify_startup_and_tasks() -> None:
    main = read_text("User/main.c")
    require_order(
        main,
        [
            "app_boot_control_vector_init();",
            "system_tasks_reset_reason_capture();",
            "rcu_all_reset_flag_clear();",
            "nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);",
            "system_tasks_create();",
            "vTaskStartScheduler();",
        ],
        "main startup",
    )

    tasks = read_text("App/system_tasks.c")
    for name in (
        "system_watchdog_task",
        "system_power_manager_task",
        "system_storage_task",
        "system_orin_status_task",
        "system_voltage_task",
        "system_rtc_task",
        "system_diagnostics_task",
    ):
        require_text(tasks, f"xTaskCreateStatic(\n        {name}", "static task creation")

    require_text(tasks, "xQueueCreateStatic(", "static power queue")
    require_text(tasks, "xEventGroupCreateStatic(", "static event group")
    require_text(tasks, "xTimerCreateStatic(", "static diagnostics timer")
    require_text(tasks, "void EXTI5_9_IRQHandler(void)", "central PB7/PB8 vector")
    require_text(tasks, "void EXTI10_15_IRQHandler(void)", "central PA11 vector")
    power_manager_start = tasks.find("static void system_power_manager_task(void *argument)")
    power_manager_end = tasks.find("static void system_storage_task(void *argument)")
    if power_manager_start >= 0 and power_manager_end > power_manager_start:
        power_manager_body = tasks[power_manager_start:power_manager_end]
    else:
        power_manager_body = ""
        fail_check("cannot isolate PowerManager task body")

    require_order(
        power_manager_body,
        [
            "g_power_on_result = orin_power_on();",
            "system_shutdown_init_after_power_on();",
            "system_startup_wait_500ms();",
        ],
        "PowerManager verified startup",
    )

    # Other modules retain optional standalone handlers inside disabled #if
    # branches.  The formal build sets all of those switches to zero, so the
    # only compiled strong handlers are the two central dispatchers here.
    exti5_count = tasks.count("void EXTI5_9_IRQHandler(void)")
    exti10_count = tasks.count("void EXTI10_15_IRQHandler(void)")
    if exti5_count == 1 and exti10_count == 1:
        pass_check("central EXTI vectors have one strong definition each")
    else:
        fail_check(
            "central handler count mismatch in system_tasks.c: "
            f"EXTI5_9={exti5_count}, EXTI10_15={exti10_count}"
        )


def verify_cold_start_fix() -> None:
    power = read_text("App/power_ctrl.c")
    require_text(
        power,
        "gpio_init(SHUTDOWN_REQ_PORT,\n              GPIO_MODE_IPU,",
        "PA11 cold-start input",
    )
    require_text(
        power,
        "if (POWER_CTRL_STATE_POWERING_ON == s_state)\n    {\n        return POWER_CTRL_RESULT_OK;\n    }",
        "ignore PA11 transient during POWERING_ON",
    )

    power_on_start = power.find("power_ctrl_result_t orin_power_on(void)")
    power_on_end = power.find("power_ctrl_result_t orin_shutdown_prepare(void)")
    if power_on_start < 0 or power_on_end <= power_on_start:
        fail_check("cannot locate orin_power_on body")
    else:
        body = power[power_on_start:power_on_end]
        state_position = body.find("s_state = POWER_CTRL_STATE_POWERING_ON;")
        shutdown_position = body.find("shutdown_request_is_active()")
        if state_position >= 0 and (shutdown_position < 0 or shutdown_position > state_position):
            pass_check("orin_power_on has no PA11 gate before POWERING_ON")
        else:
            fail_check("orin_power_on checks PA11 before entering POWERING_ON")


def verify_interrupt_policy() -> None:
    watchdog = read_text("App/watchdog_ctrl.h")
    shutdown = read_text("App/shutdown_request.h")
    power_fail = read_text("App/power_fail.h")
    expected = [
        (watchdog, "#define WATCHDOG_CTRL_DEFINE_IRQ_HANDLER              0U"),
        (watchdog, "#define WATCHDOG_CTRL_IRQ_PRIORITY                    5U"),
        (shutdown, "#define SHUTDOWN_REQUEST_DEFINE_IRQ_HANDLER           0U"),
        (shutdown, "#define SHUTDOWN_REQUEST_IRQ_PRIORITY                 5U"),
        (power_fail, "#define POWER_FAIL_DEFINE_SHARED_IRQ_HANDLER    (0U)"),
        (power_fail, "#define POWER_FAIL_IRQ_PRIORITY                 (5U)"),
    ]
    for text, token in expected:
        require_text(text, token, "interrupt policy")


def verify_scope() -> None:
    active_dirs = [ROOT / "App", ROOT / "User", ROOT / "BSP", ROOT / "Config", ROOT / "Common"]
    forbidden_names = ("gnss", "pps", "airy", "lidar", "time_sync")
    for directory in active_dirs:
        for path in directory.rglob("*"):
            if not path.is_file():
                continue
            lowered = path.name.lower()
            for forbidden in forbidden_names:
                if forbidden in lowered:
                    fail_check(f"out-of-scope active file: {path.relative_to(ROOT)}")
    if not any("out-of-scope active file" in error for error in ERRORS):
        pass_check("active source tree excludes GNSS/PPS/Airy/LiDAR/time-sync files")

    heap_files = list(ROOT.rglob("heap_*.c"))
    if heap_files:
        for path in heap_files:
            fail_check(f"dynamic heap source present: {path.relative_to(ROOT)}")
    else:
        pass_check("no heap_*.c files")


def main() -> int:
    verify_required_files()
    verify_project()
    verify_flash_layout()
    verify_freertos_config()
    verify_startup_and_tasks()
    verify_cold_start_fix()
    verify_interrupt_policy()
    verify_scope()

    for message in PASSES:
        print(f"PASS: {message}")

    if ERRORS:
        for message in ERRORS:
            print(f"FAIL: {message}", file=sys.stderr)
        print(f"\nRESULT: FAIL ({len(ERRORS)} issue(s))", file=sys.stderr)
        return 1

    print(f"\nRESULT: PASS ({len(PASSES)} checks)")
    print("NOTE: Keil ARMClang build and real-board validation are still required.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
