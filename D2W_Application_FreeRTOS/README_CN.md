# D2W Application FreeRTOS 正式任务拆分版

本目录是 GD32F303VCT6 的独立 Application 工程。它保留已经在实板验证成功的 Orin 冷启动修复和电源时序，并把长期运行逻辑正式拆成七个静态 FreeRTOS 任务。

当前正式版本包含：

- Orin 和载板电源控制
- 外部看门狗
- PA11 关机请求
- PB8 掉电事件及内部 Flash 记录
- Orin SYS_RESET 状态检测
- 四路 ADC 电压监测
- PB10 和 PB11 外部 RTC 日历时钟
- Bootloader 跳转接口及 Application 向量表重定位

当前正式版本不编译 GNSS、PPS、Airy、激光雷达或时间同步功能。`board_pins.h` 中可以保留未来接口定义，但工程中没有对应运行模块，也不会占用串口中断或创建相关任务。

## 打开工程

双击 `D2W_Application.uvprojx`。

推荐环境：

- Keil MDK 5
- ARM Compiler 6.24
- 目标器件 GD32F303VC
- ST-Link Debugger

工程已经携带 GD32F30x Firmware Library V2.1.5、启动文件、系统文件和 FreeRTOS Kernel V11.1.0，不依赖工程目录外的源码路径。

## Flash 布局

| 区域 | 起始地址 | 大小 |
|---|---:|---:|
| Bootloader | `0x08000000` | `0x00008000` |
| Application | `0x08008000` | `0x0001B000` |
| 更新暂存区 | `0x08023000` | `0x0001B000` |
| Bootloader 元数据 | `0x0803E000` | `0x00001000` |
| PB8 掉电记录 | `0x0803F000` | `0x00001000` |

Application 的 IROM1 必须保持：

- Start：`0x08008000`
- Size：`0x0001B000`

IRAM1 必须保持：

- Start：`0x20000000`
- Size：`0x0000C000`

不得把 Application 起始地址改回 `0x08000000`，否则会覆盖 Bootloader。Application 启动后第一步将 VTOR 设置为 `0x08008000`。

## 正式任务

| 任务 | 优先级 | 栈深度 | 唯一所有者 |
|---|---:|---:|---|
| Storage | 6 | 640 words | FMC 和内部 Flash 写入 |
| Watchdog | 5 | 256 words | WDI 输出和 WDO 故障处理 |
| PowerManager | 4 | 1024 words | PB14、PA7、PB15、载板电源和关机状态机 |
| OrinStatus | 3 | 384 words | PA8 状态解释和 Orin 状态机 |
| Voltage | 2 | 512 words | ADC0 和电压故障判定 |
| RTC | 2 | 640 words | I2C1 和外部 RTC |
| Diagnostics | 1 | 384 words | Watch 快照、栈余量和健康统计 |

所有任务、队列、事件组和软件定时器都静态分配。工程不编译 `heap_*.c`，不使用动态内存，也暂不启用互斥锁。这里不是遗漏互斥锁，而是通过严格的外设所有权避免共享访问。

## 启动顺序

1. `main()` 重定位 Application 向量表。
2. 读取并清除复位原因。
3. 设置 NVIC 优先级分组。
4. 静态创建队列、事件组、软件定时器和七个任务。
5. 启动调度器。
6. PowerManager 初始化安全 GPIO。
7. Watchdog 初始化并产生第一条 WDI 边沿。
8. OrinStatus 初始化 PA8 检测。
9. Storage 初始化 PB8 掉电记录区。
10. PowerManager 按已验证顺序调用 `orin_power_on()`。
11. 上电成功后才初始化 PA11 关机请求中断。
12. 延时 500 ms 后再次喂看门狗。
13. Voltage 初始化 ADC。
14. RTC 初始化 I2C1 和外部 RTC。
15. 设置 `g_boot_stage = 8`，发布启动完成事件。

`orin_power_on()` 内部仍按实板验证顺序执行：安全输出、PB14、等待、释放 PB15、PA7、PA0、PA1、PA4 和 PC8、确认 PA8 释放、进入 ON 状态。

## 必须保留的冷启动修复

- PA11 在最初 GPIO 初始化时使用 `GPIO_MODE_IPU`。
- `POWER_CTRL_STATE_POWERING_ON` 期间忽略 PA11 的瞬态低电平。
- `orin_power_on()` 开始前不使用 PA11 阻止上电。
- `shutdown_request_init()` 只在 Orin 成功上电后执行。

删除或提前这些逻辑可能重新引入“Debug 可以启动，冷启动不能启动”的问题。

## 编译和烧录

1. 打开工程并 Rebuild。
2. 必须达到 0 Error。
3. Application 可有 CMSIS 兼容性警告，但不要把真正的错误当作警告忽略。
4. After Build 的 `fromelf` 命令默认不勾选，避免新电脑找不到 `fromelf.exe` 而造成伪编译失败。
5. Keil 会自动生成 `Objects\D2W_Application.hex`。
6. Flash Download 选择 GD32F30x High-density 512 KB 算法。
7. 选择 Erase Sectors、Program、Verify。
8. 已同时烧录 Bootloader 和 Application 后，不要使用 Erase Full Chip。

先烧录 Bootloader，再烧录本 Application。以后只更新 Application 时使用 Erase Sectors。

## 文档

- `Docs/01_正式迁移流程.md`
- `Docs/02_上板测试流程.md`
- `Docs/03_工程树与任务设计.md`
- `RELEASE_NOTES_CN.md`

`Tools/verify_formal_split.py` 可在电脑上执行静态工程一致性检查。该脚本不能代替 Keil ARMClang 编译和实板测试。

## 重要发布门槛

软件模拟 PB8 只能验证队列、任务和 Flash 记录逻辑。正式发布前仍必须通过真实掉电测试，确认保持时间足以让 Storage 任务完成一次记录；否则应使用预擦除、缩短记录或增加硬件保持时间。
