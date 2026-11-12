# movzx reg,[mem] 落地（2026-11-12，8cd062b）

## 零新模板

拆开看 movzx 的两个语义：从内存读 1/2 字节、零扩展写进目标寄存器。

- `jit_load(vaddr, width)` 内部走 `bus_.read_u8/read_u16/read_u32`，返回值
  本来就是零扩展的 uint64。
- `kLoadSlot` 的原始字节（不经过 `load_code<>` 的宽度替换）结尾是
  `mov [rdi+dst32],rax`，整槽 8 字节全写。

两条拼起来，正好就是 movzx：目标 32 位时高位本来就 0，目标 64 位时零
扩展到底。所以这批没有新增模板，只是把 `kLoadSlot` 当原材料用了一次。

要区分的是两件事：`load_code<kLoadSlot>(width)` 会按宽度把结尾换成一字节
或两字节的 merge 写（普通 mov 的语义），而 movzx 要的是不换——直接
`std::span<const uint8_t>(kLoadSlot)` 拿原始数组。

## 测试的两个坎

第一版用例写 `movzx eax,byte [rsp]`，以为 `[rsp]` 指向播种的数据。夹具里
rsp 是 0xF000，数据在 kData（0x4000），读到的是零。

改成 `[r12]` 又错一层：夹具把 base 寄存器设成 `kData + 0x48`（给带
displacement 的用例留空间），所以要显式把用例的 `.base` 覆盖成 `kData`。

第三层错在期望值：非 store 用例的 `.memory_initial` 默认是 `kLoaded`
（0xFEDCBA9876543210），不是 `kStored`。低字节读出来是 0x10 而不是 0x11。

三层都过了，才拿到正确断言。**夹具的默认值要逐个确认，不能从用例名猜**。

## 行数

分支一版写在 `jit_translate.cpp`，把文件顶到 607 行。搬到
`jit_translate_mov.cpp` 一次解决：mov 族自己的地盘只有 318 行。搬之前
确认分支自带完整宽度门（操作数宽度、段寄存器、disp 范围都判过），不
依赖 `translate_insn` 中段那道守卫——这正是之前拆 mov 族时留下的教训。

clang-format 又把 `{.offset = 12, .size = 4, .value = static_cast<...>}`
折成三行，`readability-trailing-comma` 立刻报。照旧的处理：把 cast 提到
局部变量 `disp`，行缩短后不再折行。

## 结果

老内核 97.09% → **97.12%**，新内核 97.07% → 97.11%，双内核 console 逐字节
一致。303 差分用例，九段验收全绿。

剩余失败主体：`add/sub/and/or reg,[mem]`（约 500 次）、`lea reg,[mem]` 102、
`cmp [mem],imm` 86、`pxor xmm,xmm` 75。
