# SSE 搬移的 index 形式（2026-11-13，8aff9d2）

## 模板

`kMovxIdxSlot`（60 字节）在 `kMovxStoreSlot` 的地址序列里插了一段 SIB 计算：

```
push rdi ; mov rsi,[rdi+base32] ; mov r8,[rdi+idx32] ; shl r8,shift8 ;
add rsi,r8 ; add rsi,disp32 ; mov rdi,cpu64 ; mov edx,slot32 ;
movabs r11,helper64 ; call r11 ; pop rdi
```

helper 仍是 `jit_load_xmm` / `jit_store_xmm`——16 字节搬移本来就只认
"地址 + xmm 槽"，地址怎么算出来的与它们无关。

`mem_ok` 放宽到允许 index，但要求 scale 是 2 的幂且不超过 8；不满足的
仍然回退解释器。

## 结构

整个 movx 族（74 行）拆进 `jit_translate_movx.cpp` / `.hpp`，
translate.cpp 从 610 行回到 537。这是第五个从 translate.cpp 分出来的
翻译单元了（mov、alu、control、thunks、movx）。

## icount 的一次变化

这批做完，`icount` 从 28,134,863 变成 28,135,989（多了 1126 条），
console 与终态 rip 都没变。这是块边界变化引发的中断注入时机差异——已知
的块粒度效应，判据仍是 console 逐字节一致。

**看到 icount 变要立刻验证 console**，不能因为"只差一千多条"就放过。

## 结果

失败块 406 → 386，覆盖率 99.9611%（768 次回退摊在 2800 万条里，百分比
只动千分之一）。321 差分用例，九段验收全绿。

剩下的 386 个块基本都是 `lgdt`、`fxsave64`、`rdmsr`、`iret` 这类要建模
架构状态的指令，每项编译失败十几次、执行一两次。
