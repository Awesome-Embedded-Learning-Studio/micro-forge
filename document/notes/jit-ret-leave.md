# ret / leave JIT lowering（2026-11-11，c1e7044）

## 机制

块模型原本只有"顺序执行 + rip+=length 退出"。ret 打破这一点：

- 新增 `jit_next_rip_/jit_next_set_`：helper `jit_ret(sp, my_rip)` 读
  [rsp]（走 jit_load 的 translate_access，含故障注入）、rsp+=8、把弹出
  目标写进退出状态；`run_jit_block` 检查标志，有则 `rip_=target` 代替
  `rip_+=length`。
- 故障路径：栈读失败时 rsp 不动，置 `jit_suspend_=true` 并把退出目标设
  为 ret 自己的 rip（模板里 patched 常量，movabs rdx,my_rip64）。下一
  个 step 先查 suspend 标志，直接回解释器重执行 ret，故障以慢路径语义
  交付——避免"块首即 ret"时的 JIT 无限重入。
- `kRetHelper` 44B 自带宿主 ret 结尾；compile_block 的 budget 逻辑原本
  就在 ret 处截断，不需要尾模板。
- `kLeaveHelper` 25B：`jit_leave()` 直接复用解释器的 pop_value +
  write_gpr，纯块内指令。ret imm16（C2）继续留解释器（少且带栈清理）。

## 值得记的

1. 这是块链接的第一块砖：动态退出目标已经打通，call（压返回址 + 跳）
   只是"store + ret 机制"的组合，jcc-in-block 则需要两条出口路径。
2. 差分 harness 的 leave 用例：非 store 用例的 memory 检查默认期望
   memory_initial==kLoaded，改播种 kStored 时必须显式给 memory_expected。
3. ret 用例用 push imm32 自造栈目标（0x1006 = hlt 地址），块在 ret 截断，
   jit_insns=2。

## 结果

老内核 78.63% → 79.60%，新内核 78.61% → 79.58%（+0.97pp，ret 动态
占比高于静态 3.8% 的印象——顺带 leave 和更长的块尾拼接）。287 差分
用例，475 项 GoogleTest 全绿，九段验收通过，console 逐字节一致。
