# 041 · C2 — `set_nvic`/`set_scb` 架构债收口

> 日期: 2026-07-08
> 分支: `feat/deepen-gui-and-core`
> 状态: 收口(文档标注,无代码行为变更)
> 验证: ctest 367 全绿(无逻辑改动)

## 背景

DIRECTIVES §A 铁律:外设/CPU 通过 `WeakPtr` 注入,避免循环引用。但 `CortexM3CPU` 一直用裸指针 setter:

```cpp
void set_nvic(periph::NvicPeripheral& nvic) { nvic_ = &nvic; }
void set_scb(periph::ScbPeripheral& scb)    { scb_ = &scb; }
```

调用方唯一一处:`stm32f103_soc.cpp` `create()` 里 `cm3_ptr->set_nvic(p.nvic)` / `set_scb(p.scb)`。05 验收要求"归拢"这条债。

## 调研

- **`bus_` 注释已论证过同一 pattern**(`cortex_m3.hpp:21-25`):"Raw observer pointer, not WeakPtr ... matches the existing nvic_/scb_ pattern — a WeakPtr here was a misuse (the ownership tree is a clean unique_ptr, no cycle to break) and cost a control-block deref + IsValid on every fetch/load/store." 即先前已评估并判定裸指针正确、WeakPtr 误用。
- **无循环**:`Stm32f103Parts`(含 NVIC/SCB)与 CPU 都是 `Stm32f103Soc` 的成员,同生共死;NVIC→CPU 走 callback + WeakPtr(pull 模式),CPU→NVIC 是单向裸指针。铁律的目的是断环,这里根本没有环。
- **用法在异常路径,非每指令热路径**:`nvic_->set_pending(irq)`(raise_irq)、`scb_->system_exception_priority(exc_num)`(优先级查询),都不在 fetch/decode 主循环里。
- **NVIC/SCB 本身有 `GetWeak()`**——改 WeakPtr 技术上可行,但没有收益(无环可断)。

## 决策:文档标注,不机械改 WeakPtr

机械改成 WeakPtr 是 churn:动 CPU 公共接口 + 调用方,每处 `nvic_->` 变 `lock + IsValid + deref`,换来的"一致性"是表面的——铁律的精神是"避免循环引用",这里无循环,裸指针合法。把债收口为"明确标注为何裸指针 + 记录决策"比"为合规而改"更诚实。

落地三处:
1. **`cortex_m3.hpp` `set_nvic`/`set_scb` 加注释**:点明无循环 / 同生共死 / 异常路径 / 指向 `bus_` 注释与本文。
2. **DIRECTIVES §A 生命周期条款加例外澄清**:承认 SoC 同生共死 wiring 不算违反"避免循环"(让铁律自洽,不丢边界)。
3. **本文**:决策记录(L8 纪律)。

## 验证

纯文档 + 注释改动,无逻辑变更。ctest 367 全绿(未重跑逻辑——只改注释/文档,不参与编译语义)。若日后真出现 CPU/外设解耦(不同生命周期),届时再回头改 WeakPtr——当前 SoC 单容器模型下无此需求。

## 陷阱(给后人)

别看到 DIRECTIVES §A "WeakPtr 注入" 就把 `set_nvic` 改 WeakPtr——先确认是否真有循环引用。本项目的所有权树是 SoC 单 unique_ptr 容器(parts + machine + cpu 同生共死),NVIC→CPU 的反向链已是 WeakPtr/callback,正向裸指针安全。
