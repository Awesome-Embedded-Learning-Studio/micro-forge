# 008 — Hook 事件总线:给模拟器装"钩子"

> 2026-06-19。把分散的 per-peripheral callback 归拢成一个统一、类型化、不阻塞的事件总线,GPIO 翻转 / UART 字节 / 自定义点都能被"抓"到。

## 动机

模拟器本来就埋了几个观察点,但各管各的:

- `periph::Gpio::set_pin_change_callback` —— GPIO 翻转
- `periph::SerialPort::set_output` —— UART 字节
- `memory::Bus::set_trace` / `tools::enable_mmio_trace` —— 总线访问
- SCB 的 vtor/prigroup callback

想"快速投一份不阻塞的日志""看 GPIO 到底翻没翻",得各自接一遍,没有统一入口、没有时序、没有批量。要一个**总线**: peripherals 发事件, 观察者订阅, 投递不拖慢模拟。

## 设计

四块, 全 header(`include/hooks/`):

### `Signal<E>` — 类型化 observer
```cpp
template <typename E> class Signal {
    using Slot = std::function<void(const E&)>;
    Token connect(Slot);          // 返回 token, 可 disconnect
    void  emit(const E&) const;   // 同步派发给所有 slot
};
```
一个事件类型一个 Signal。emit 按 connect 顺序同步调每个 slot。

### `RingSink<E>` — 不阻塞收集器
```cpp
RingSink<GpioEdge> ring(1024);
signal.connect(ring.slot());      // slot = O(1) push 进有界 ring
auto batch = ring.drain();        // 事后批量取走, ring 满了丢最旧并计数
```
SPSC ring, push 是 `head.fetch_add + buf[idx%N]`, **恒定时间、不阻塞**。模拟热路径只 push, 不做格式化/IO; 重活由消费者事后 `drain()`。

### `events.hpp` — 语义事件
```cpp
struct EventHeader { uint64_t cycle; };     // 时序锚点
struct GpioEdge : EventHeader { char port; uint8_t pin; bool rising; };
struct UartByte : EventHeader { uint8_t unit; uint8_t byte; };
// 想抓什么, 加个 struct + 一个 Signal 即可
```

### `EventBus` — 聚合
```cpp
class EventBus {
    Signal<GpioEdge> gpio;
    Signal<UartByte> uart;
    void set_cycle_source(CycleSource);     // 平台盖时间戳, 外设不用懂时钟
};
```

## "不阻塞"怎么说清楚

模拟器是单线程、确定性、可重放的 —— 不能塞线程池(会破坏确定性)。所以"不阻塞"靠**纪律**, 不是靠并发:

- `emit()` 是 O(slots) 同步派发。
- slot 的契约是**轻量**: push 进 ring、翻个 flag、累个计数。
- 格式化 / IO / 网络 —— 任何可能拖慢热路径的 —— 进 ring, 由消费者离线 drain。

RingSink 把"接收"和"处理"解耦: 接收永远快(O(1)), 处理可以慢/可以后台。这就是"不阻塞的快速投递"。

## GPIO 怎么挂上

`Stm32f1Gpio` 已经在所有 ODR 写(ODR / BSRR / BRR / `set_pin`)后调 `on_odr_changed(old, new)`, 它做 pin diff。在那里 emit:

```cpp
void Stm32f1Gpio::on_odr_changed(uint32_t old_odr, uint32_t new_odr) {
    uint32_t changed = old_odr ^ new_odr;
    for (uint8_t i = 0; i < 16; ++i)
        if ((changed >> i) & 1) {
            bool high = (new_odr >> i) & 1;
            if (on_pin_change_) on_pin_change_(i, high);          // 旧 callback 保留
            if (!edge_signal_.empty())
                edge_signal_.emit({ {cycle_source_()}, port_id_, i, high });
        }
}
```
SoC 在 CPU 起来后给每个 GPIO 注入 cycle source(`cm3->cycles()`), 事件就带上了模拟周期时戳。

## demo: 实抓 F103 的 PA1

`examples/hook_demo/runner.cpp` 订阅 GPIO edge, 跑真实 Keil 固件, 两个 subscriber —— 一个 live 打印、一个 RingSink 批量:

```
--- running firmware (max 2,000,000 steps) ---
[live ] GPIOA.1 RISE @ cycle 663
[live ] GPIOA.1 FALL @ cycle 64773
[live ] GPIOA.1 RISE @ cycle 65583
...
[rcc ] CR=0x03030083 CFGR=0x001D040A  PLLRDY=1 SWS=2
--- direct PA1 toggle demo ---
[live ] GPIOA.1 FALL @ cycle 2000000
[live ] GPIOA.1 RISE @ cycle 2000000
--- drained 63 buffered edge(s), 0 dropped ---
```

PA1 的边沿被**语义化**地抓到了(端口、引脚、方向、周期), 不是原始 MMIO 字节。

## 顺带: 这趟还修了 CPU 的第 8 个 bug

为了让 F103 真正驱动 GPIO(而不是卡在 SystemClock_Config), 定位到 16 位 data-proc register 的 **Rm 字段取错位**:

```cpp
// 旧: rm3(insn) = bits[8:6]   ← 这是 op 的低 3 位!
// 新: Rm = bits[5:3]           ← ARM 16-bit data-proc register 的真字段
```
`orrs r1, r0` 因此把 R4(=RCC 基址) 当成 Rm, 结果的 SW 位恒 0, PLL 永远切不上。修了之后 `SWS=2`(PLL), 固件到 main 写 PA1。

> 注: F103 到 main 后还有一处 SysTick 中断返回 corrupt(PC 飞到 `0x00020000`), 是下一批要查的; 不影响 hook 机制本身 —— demo 里 PA1 的周期翻转就是异常返回回到 main 的副作用, hook 照抓不误。

## 扩展

- UART: 现有 `set_output` 指向 `bus.uart.emit(UartByte{...})` 即可归一。
- 自定义: 任何想"抓"的点变成 `struct XxxEvent : EventHeader` + 一个 `Signal<XxxEvent>`, emit 处加一行。
- 后台消费者: RingSink drain 可以放到独立线程(它是 SPSC 安全的), 真正做到模拟线程零等待。
