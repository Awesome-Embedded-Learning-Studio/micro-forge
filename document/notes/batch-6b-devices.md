# 批 6b 端口 IO 与首批设备 — 笔记

## 交付

- **端口空间**（`kernel/port_space.hpp`）：16 位端口空间，区间注册、重叠拒绝、宽度路由（1/2/4）。`PortDevice` 接口收 offset+width，设备自己决定宽度语义。与内存 Bus 分开——x86 两个地址空间，别混。
- **in/out 执行**（`src/arch/x86_64/io.cpp`）：imm8 端口（0xE4-0xE7）与 DX 端口（0xEC-0xEF）两种形式；未映射端口 fault（code=端口号，detail=kMemory）。注意 x86 语义：imm8 形式里立即数是**端口号**，数据只在 AL/AX 里——写测试时第一版把「端口+数据」都塞进 imm 是错的，实际 out imm8 只能写累加器低字节。
- **COM1 16550**（`kernel/serial_16550.hpp`）：轮询面。LSR.THRE|TEMT 常置（发送永远可排空）、DR 清零（无输入源）；DLAB 锁存切换除数寄存器；THR 写入进机器 console。Cinux 的串口初始化序列（IER=0/LCR=0x80/divisor=1/LCR=0x03/FCR=0xC7/MCR=0x0B）原样接受。
- **PIT 8254**（`kernel/pit_8254.hpp`）：三通道 + 控制字状态机（access 模式、lo-hi 组装、divisor 回读）。ch0 方波（0x36）与 ch2 单次（0xB0）都接受。**主板端口 0x61**（`BoardControl`）单独建模：bit0/1 锁存门控与扬声器，bit5 恒报「定时器 2 输出完成」——虚拟时钟按 icount 推进，等待循环不能真等墙钟，微秒延迟以立即完成处理（真实周期性归 6c 中断管线）。
- **RTC**（`kernel/rtc.hpp`）：0x70 索引 + 0x71 数据，BCD 时间固定为 2026-01-01 12:00:00（种子派生墙钟等批 8 的 seed 落地），状态 A 报无更新进行中，状态 B 报 24 小时 BCD。
- **PIC 8259A**（`kernel/pic_8259.hpp`）：ICW1-4 初始化序列状态机 + OCW1 掩码读写。两个实例（主 0x20、从 0xA0）。本批只建模寄存器状态，不投递中断（6c 的活）。
- Machine 装配：COM1(0x3F8,8)、PIT(0x40,4)、板控(0x61,1)、RTC(0x70,2)、主从 PIC。

## 测试

DeviceTest 5 项（PIT 方波除数、板控 bit5、RTC 状态 B、PIC 重映射向量基、PIC 掩码读改写）+ PortTest 3 项。141 项全绿。

## 开放项

- PIT/LAPIC 的周期事件与中断注入（6c）。
- RTC 的 seed 派生（批 8）与 BCD 时间推进（虚拟时间按 icount 推进时 RTC 应同步走钟；6b 固定值先让引导跑通）。
- 8042、PCI、AHCI（6d）。
