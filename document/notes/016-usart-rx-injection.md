# 016 — USART RX 注入 + RXNE 中断(C3)

> 06 第二波 C3(收尾)。USART 加 RX 注入(`inject_rx` 单字节 + RXNE)+ RXNEIE 中断 → raise USART1(IRQ37)。raise_irq **第三消费者**(TIM/EXTI/USART 全通)。TXEIE 跳过(模拟器 TX 即时,TXE 常高会循环 raise)。ctest **301/301 绿**(296 + 5 新)。

## 实现

1. **USART `inject_rx(byte)`**:设 `rx_dr_` + SR.RXNE(bit5);RXNEIE(CR1 bit5)使能 → `irq_callback`。
2. **DR read 返回 rx_dr_ + 清 RXNE**(硬件 DR 读=RX/写=TX 共享地址)。
3. **CR1 write 使能 RXNEIE 时若 RXNE 已置** → 立即 raise(固件先收字节后开中断的场景)。
4. **`kUsart1Irqn=37`**(interrupt_config)。SoC `usart1.set_irq_callback(raise 37)`。

## 验证(5 新)

- **单元**([test_stm32f1_periph.cpp](test/test_stm32f1_periph.cpp)):`inject_rx`→RXNE、DR read 清+返回字节、RXNEIE enabled→callback、disabled→不 callback。
- **E2E**([test_interrupt_roundtrip.cpp](test/test_interrupt_roundtrip.cpp) `UsartRxRoundtrip`):`inject_rx('A')` → USART1 IRQ37 → handler `ldr r4,[r1]` 读 DR → r4='A'。
- `ctest` 全量 **301/301 绿**,无回归。

## 陷阱

- **ARM 异常模型**:exception entry 自动压栈 r0-r3/r12/lr/pc/xpsr,exception return POP 恢复。handler 改 r0-r3 会被返回覆盖。测试读 handler 结果须用 **r4-r11**(不自动压栈,返回后保留;handler 本应 push/pop 保护 —— 测试简化可省)。
- **IRQ≥32 在 ISER1/ISPR1**:NVIC ISER `idx=offset/4`,ISER1(0x004)enable IRQ32+(37→bit5)。IPR IRQ37 在 `0xE000E424`(IPR9)byte1,word 写需 `<<8`。
- **TXEIE 跳过**:模拟器 TX 即时完成,TXE 常高(`0xC0`);TXEIE 会循环 raise(无 TX 移位延迟可消耗)。固件用 TXEIE 时轮询 TXE 位仍工作。MVP 不做 TXEIE。
- **USART DR 共享**:读 DR=返回 RX+清 RXNE;写 DR=TX(原逻辑)。`dr_` 成员 write 存但 read 不返回(无害)。

## 成果

第二波外设中断端到端**全部完成**:raise_irq 公共通道(C2)+ TIM(C2)+ EXTI(C1)+ USART RX(C3)三消费者全通同一通道。06 第二波 C1/C2/C3 + C4(bit-band 早前)✅。下一里程碑候选:第三波 DMA/SPI/FLASH、04 GUI dashboard、02 收尾。
