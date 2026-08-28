# D2W Application v1.1.0 发布说明

日期：2026-08-26

## 本版完成

- 把单循环正式拆为七个静态 FreeRTOS 任务。
- 使用静态任务、静态队列、静态事件组和静态软件定时器。
- 明确 PowerManager、Storage、Watchdog、RTC、Voltage 和 OrinStatus 的外设所有权。
- 使用命令队列把复位、上电和强制断电请求统一交给 PowerManager。
- 使用事件组发布初始化、运行和故障状态。
- 使用任务通知把 PB7、PB8 和 PA11 中断交给对应任务。
- 把 PB8 Flash 保存从 ISR 移到 Storage 任务。
- 保留经实板验证的 Orin 冷启动修复和上电顺序。
- 保留 Bootloader 的固定 Application 地址和向量表重定位。
- 暂不编入 GNSS、PPS、Airy、激光雷达和时间同步运行模块。

## 有意未启用

- 动态内存和 `heap_*.c`
- 互斥锁
- 运行时串口日志
- GNSS 和雷达相关任务
- Bootloader 源码改动

## 发布前必须验证

- Keil ARM Compiler 6.24 Rebuild 为 0 Error。
- Bootloader 和 Application 分区未重叠。
- 至少 20 次完全断电冷启动。
- Orin Ubuntu 每次都能登录。
- PA11 关机、PB15 主动复位、外部看门狗超时和恢复。
- 四路 ADC 合理并能产生及恢复故障。
- RTC 走时和复位保持。
- PB8 软件记录测试及真实掉电保持时间测试。
- 七个任务栈高水位保留安全余量。
