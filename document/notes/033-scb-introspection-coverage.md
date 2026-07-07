# 033 — SCB introspection 摘要 + scb/nvic coverage 收口(G3)

> 日期: 2026-06-30
> 阶段: v0.7.0 GUI 前置([milestone 04](../milestones/04-gui-debug-dashboard.md))
> 状态: ✅ ctest 328→352 双构建绿 + bench advisory PASS
> 前置: G1(notes 031)/ G2(notes 032)

## 背景

G2 补了 GPIO/SysTick/NVIC introspection,但 SCB(系统控制块)摘要没做——GUI 系统/异常面板要 ICSR(pending exception)/ VTOR(向量表基址)/ AIRCR(prigroup)。同时 coverage 战役 scb 29% / nvic 62% 是用户选的"GUI 前置收口"目标。

## 做了什么(G3,两步合一 commit)

### G3a — SCB introspection 摘要
1. **ScbPeripheral 加 4 个 inline const getter**:`icsr()/vtor()/aircr()/prigroup()`(PRIGROUP = AIRCR bits [10:8])。
2. **PeripheralsSnapshot 加 `ScbSnapshot{icsr,vtor,aircr,prigroup}`**;read_introspection 填(parts.scb.*);snapshot.cpp 序列化 peripherals 区加 scb。
3. test_introspection 加 SCB 默认值断言(AIRCR = 0xFA050000)。

### G3b — scb/nvic coverage 收口
4. **新建 `test_scb.cpp`(14 测试)**:cpuID / ICSR / VTOR(含 callback)/ SCR / CCR / SHCSR 读写 + AIRCR VECTKEY 校验(错 key 静默忽略、对 key 触发 prigroup callback)+ SHP 三组 word + byte 写 + Unaligned / PeripheralFault + system_exception_priority 边界。
5. **test_nvic.cpp 加 10 测试**:Unaligned / Unmapped 错误路径 + ICER 读 = ISER / ICPR 读 = ISPR 别名 + `highest_priority_pending_irq`(优先级加权 + 空 + cache 命中)。

## 挖到的真实 bug(coverage 收口的收益)

**SCB CCR(offset 0x14)write 未实现**:read switch 有 `case 0x14`,write switch 漏了 → 写 CCR 直接走 default 返回 `PeripheralFault`。ARM v7-M CCR 是 read/write。补 `case 0x14: ccr_ = data; return {};`。

- 现有 ctest 全绿(没固件写 CCR),所以一直没暴露——正是覆盖率盲区的典型形态。
- 新测试 `ScbTest.ScrAndCcrReadWrite` 用**非默认值** 0x300 pin 住 fix(default ccr_=0x200,写 0x300 读回 0x300 才证明真写)。
- oracle_cortex_m3 仍对齐(bench 固件不写 CCR,或写了对齐)。

这是 COVERAGE-METHODOLOGY §0 预判的"scb gap 补强可能挖出真实 bug"的实例——正是先验承重墙再装门牌的价值。

## 坑(测试预期错,非实现 bug)

- **SHP word 字节序**:`write 0x18=0x11223344` → byte0(`0x44`)进 `shp_[0]`=exc4,byte3(`0x11`)进 `shp_[3]`=exc7(小端)。第一版测试把 exc↔byte 对应写反了——read round-trip 是对的(`0x11223344` 写读一致),只有 `system_exception_priority` 的预期反了。修正后绿。

## 验证

- ctest **352/352**(Debug + RelWithDebInfo;原 328 + test_scb 14 + test_nvic 10)。
- bench `--baseline` advisory PASS(0 regression;scb.cpp 改一个 case,不在每指令热路径)。
- oracle_cortex_m3 过(CCR fix 不破坏 QEMU 差分)。
- gcov 预期:scb 从 29% 大幅上调(14 测试覆盖全部 read/write 分支)、nvic 从 62% 上调(补错误路径 + read 别名 + 优先级)。具体数值待 `build-cov` 跑 gcovr。

## 下一步

- **G4**:GuiSession 控制封装(step/pause/reset,把 CLI 100k-chunk run loop 抽成 GUI 可调 API,不破坏 CLI)。
- **G5**:Qt6 GUI MVP(QMainWindow + QDockWidget,全量 snapshot)。
- **EventBus 推迟到 GUI 后按需**(YAGNI;GUI MVP 用 30Hz 全量 read_introspection 足够看 GPIO 翻转/UART)。
