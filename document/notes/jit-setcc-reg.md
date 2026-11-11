# JIT setcc reg8 落地收工笔记（2026-11-11，`cb63f27`）

## 结果

- 老内核 b58e271 覆盖 77.4134% → **78.1779%**（+0.76pp）；新内核 a234e96 → **78.1561%**
- 双内核 console 与解释器逐字节一致；462 项测试、九段验收门禁全绿
- 新增 10 个 setcc 差分用例（e/ne/b/a/ae/le/be/g × cl，e/ne × r9b）

## 模板（kSetccRegHelper，35 字节）

```
57                  push rdi
BE <dst32>          mov esi,slot          ; 第2参
BA <cc32>           mov edx,cc            ; 第3参（edx！不是 ecx）
48 BF <cpu64>       movabs rdi,cpu
49 BB <helper64>    movabs r11,helper
41 FF D3            call r11
5F                  pop rdi
```

helper：`Cpu::jit_setcc(slot, cc)` → `merge_width(dst, condition(cc)?1:0, 1)`，
单字节写保留高 7 字节。洞：dst@2、cc@7、cpu@13、helper@23。

## 坑（又是参数寄存器）

1. **第 3 参在 edx**。首版写成 `B9`（mov ecx），helper 收到 ecx 里的未初始化垃圾
   （0x59583820），condition(垃圾) 恒假 → 写 0。定位：helper 里临时 fprintf 入参，
   一眼看出 esi/rdi 都对、唯独第 3 参是代码页字节样式的值。**SysV 参数序口诀：
   rdi,rsi,rdx,rcx,r8,r9——写模板先数清楚参数位。**
2. **setcc 操作码表**：90 o/91 no/92 b/93 ae/94 e/95 ne/**96 be/97 a**/98 s/99 ns/
   9a p/9b np/**9c l**/9d ge/9e le/9f g。首版把 9C 当 setbe（实际是 setl），
   差分期望全靠 /tmp/t_mini.cpp 解释器真值对拍修正。
3. 模板总长算错一次（声明 37、实际 35，尾部两个 0 字节被当 `add [rax],al` 执行，
   child SIGSEGV）——**array 长度=逐项累加和，写完再数一遍**。

## 覆盖率走势

33% → 50%+（寻址）→ 75.00% → 77.40%（ALU 全宽）→ 77.41%（cmov reg）→ **78.18%（setcc）**

## 下一步候选

- push/pop ~6.2%：helper 里 rsp_ -= 8、写内存、读内存；注意与解释器完全一致的
  栈序和宽度（66 前缀 2 字节 push）
- movups ~2.8%：SSE 128 位
- call/ret ~7.7%：块内控制流前置
- jcc-in-block ~61%：最大项
