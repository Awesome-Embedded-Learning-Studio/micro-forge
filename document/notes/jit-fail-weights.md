# 执行权重统计，与两个 21 万次的热点（2026-11-12，e0e6295）

## 为什么需要这个统计

之前挑目标看的是 `MICRO_FORGE_JIT_FAILSTATS` 打印的编译失败次数。那个数字
数的是"有多少个不同的 rip 编译失败了"，不是"这些块被执行了多少次"。
`lgdt` 排第一（34 次）却只在启动时跑一次，而某个循环里的指令可能编译只
失败 10 次、却执行了二十万次。

新统计在 `run_jit_block` 未命中块、且该 rip 在失败集里时计数，进程退出前
按次数降序打印前 20。数据放在 `jit_stats.cpp`（静态 map，env 门控），因为
jit_cpu.cpp 已经没有行数余量。

第一次跑出来：总回退 681,422 次，正好对上那 2.4% 的缺口。

## 两个热点

```
hits=211584  rip=0xffffffff80205ce7  0F 45 4F 48  cmovne rcx,[rdi+0x48]
hits=211584  rip=0xffffffff80200ad5  41 0F A3 D5  bt r13d,edx
hits= 66304  rip=0xffffffff8022aea4  48 83 6B 18 01  sub qword [rbx+0x18],1
hits= 66304  rip=0xffffffff8022ae9b  48 F7 D2      not rdx
hits= 26448  rip=0xffffffff80200a9e  48 0F AF 45 B0 imul rax,[rbp-0x50]
hits= 26448  rip=0xffffffff80200aee  48 83 45 C0 01 add qword [rbp-0x40],1
```

前两个各 21 万次，加起来就是 1.5 个百分点。

## bt 的做法

`bt r/m, r` 只改 CF：CF = 被测值的第 (索引 & (位宽-1)) 位。三参数模板
`kAluRegHelper`（slot、源槽、宽度）正好够用，新增 helper `jit_bt` 只算
这一位，不写回任何寄存器。

## cmov 内存形式的语义坑

第一版写的是"条件不成立就直接返回，连内存都不读"。这是错的：**x86 的
cmov 无条件读源操作数**，即使条件为假，内存访问照样发生、缺页照样触发
#PF。正确顺序是先 `load`，失败了就返回（让解释器重跑），成功了再判断
条件决定写不写。

模板 55 字节，五个参数：vaddr、cc、目标槽、宽度，加两条重定位。第一版
`call_offset` 写成了 41，实际 `49 BB` 占 @41-42、helper 洞从 @43 开始，
偏移错一位的后果是覆盖率掉了 1.5 个点（块执行到错误的地址）。

## 结构

`jit_templates.hpp` 涨到 606 行，把内存族（kLoadSlot、kStoreSlot、
kLoadIdxSlot、kStoreIdxSlot、kLeaIdxSlot32 等 226 行）拆进
`jit_templates_mem.hpp`。两个头都是纯 constexpr 表，没有编译单元。

## 结果

老内核 97.58% → **99.09%**，新内核 99.09%，双内核 console 逐字节一致。
317 差分用例，九段验收全绿。失败块 518 → 517（块数没怎么变，变的是这些
块不再被反复回退）。

下一步的候选（按权重）：`sub qword [mem],imm8` 66k、`not rdx` 66k、
`imul rax,[rbp-0x50]` 26k、`add qword [mem],imm8` 26k。
