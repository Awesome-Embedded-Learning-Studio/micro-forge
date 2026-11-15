# wasm 寄存器桥接（方案乙 stage 1）

日期：2026-11-13　分支：`feat/jit_support`

## 目标

把 `mf_wasm_block2` 产出的 wasm 块模块接到 JS 运行循环里真跑：块内寄存器运算在
wasm 里执行，控制流与不支持指令回到解释器。验收标准与 native JIT 同级：与解释器
**逐字节一致**。

## ABI（v2）

- `block() -> i64`：无参，返回下一 guest rip（ret 目标）；16 个 i64 局部桥 GPR
  （slot = reg*8，块模块自己的 linear memory）；flags cell 在 offset 256
  （i32，packed `cf|zf<<1|sf<<2|of<<3|pf<<4|af<<5`）。
- 局部声明 `{16, i64}, {2, i64}, {1, i32}`：kTmpA=16（原 dst）、kTmpB=17（源）、
  kTmpP=18（奇偶折叠链）。
- 切片：仅 qword `mov/add/sub/and/or/xor` 的 reg,reg / reg,imm32 形态；ret 不入块
  （exports 在它前面断块，块返回它的 rip，循环交 `mf_run(1n)` 执行）。
- flags 语义逐条镜像解释器 `alu_add/alu_sub/alu_logic + set_zsp`：add CF=res<a
  （i64.lt_u）、sub CF=a<b；OF 按 `(a^b)/(~(a^b)^a^res)` 符号位；AF 第 4/3 位；
  PF 用 i32 折叠链 `x^=x>>4; x^=x>>2; x^=x>>1` 后 `1 ^ (p&1)`；逻辑运算清 cf/of/af。
  **i32.eqz 不吃 i64**——zf 用 `kI64Eqz (0x50)`。
- `WasmModule` 发射器自动补 0x0B end 字节——body 里**不要再放 kEnd**。

## JS 侧正确性四件套（缺一个就分叉）

1. **BigInt 缓存 key**：kernel rip ~1.8e19，`Number(rip)` 在 2^53 以上丢精度，
   不同 rip 撞进同一个错误块 → 无声死循环。
2. **mf_retire(count)**：块执行绕过 `Cpu::retire`，调度器（PIT 100Hz、串口）会
   滞后，guest 读到不同设备状态。每次块执行后补账。
3. **refused 步进按 Δicount 记账**：`rep movsd` 一条指令 retire 12 次
   （cpu.cpp 每迭代 `retire(1)`），按 1 记账会让两侧 icount 相位漂移。
4. **fetch 无副作用**：`fetch_paddr` 原先走 `translate_access`，翻译失败会
   raise #PF 并 latch 进 `exit_`——块编译探测跨页取指时对未映射页探测，注入了
   **假 #PF**，之后第一次 `mf_run(1)` 先消费它。现改为直接 `mmu_.translate`
   （walk-only，不 latch）。
5. **event_budget 门**：块不得超过下一调度事件（`Cpu::event_budget()`，pending
   中断时为 1），否则块尾才 dispatch、中断迟到几条——native 链的 chain budget
   同款约束。

## 验证

- lockstep 对拍（/tmp/lockstep.mjs 模式）：两实例按 guest icount 严格对齐，每单位
  比较 rip + 16 GPR + 6 flags，**4000 万条全程一致**。
- console 逐字节：40M 预算下 interp 与 blocks 的 1238 字节 console sha256 相同
  （`32f136007e99c695`）。
- node 单元（/tmp/block-test.mjs 模式，已并入 block-bench 冒烟）：alu-imm+reg、
  passthrough、ret-boundary、flags-wrap 四例。
- 九段验收全绿（build/test/format/doxygen/tidy/sanitizer/wasm/differential×2）；
  native probe 不回退：coverage=99.9625%，console=1687 与基线一致。

## 诚实性能（40M 条，同一窗口）

| 模式 | Mips | 说明 |
| --- | --- | --- |
| interp | 29.95 | chunked mf_run |
| blocks | 5.46 | 桥开销 > 块收益 |

- compiled=6763 块，blockExecs=37900，**块执行占比 0.09%**——内核指令流里
  连续的纯寄存器 qword ALU 段太少，大量 rip 走 refused 单步。
- 每次块执行要跨 32 项 GPR+flags 拷贝（JS ↔ 块模块 memory），这是主导成本。

## 下一步（未做，按收益排）

1. **加宽切片**：cmp/test+jcc 入块（让块真正终结于分支而不是顺序断块）、
   guest 内存 ld/st、call/jmp。
2. **engine memory 导入块模块**：块直接读写引擎内存，消掉逐次拷贝。
3. 页面接线暂缓：块占比 0.09% 时接线只会拖慢 demo；等切片加宽后再评估。

## 2026-11-14 跨页取指修复 + 失效精度（正确性收官）

- **根因（40M 对拍停在 ic 10961302）**：`mf_wasm_block` 用 `ram().view(paddr,16)`
  平面物理窗口取指，跨**虚拟页**边界的指令把 imm/displacement 读成物理相邻页的
  字节（`83 e2 01` 的 imm 在下一虚拟页、物理 0x1b5000，平面读拿到 0x1b7000 的 0）。
  解释器逐字节走 MMU 一直是对的——是块编译器错。诊断教训：fetch_peek 的平面窗口
  读会撒谎，两边都必须逐页翻译。
- 修复：块编译改逐页翻译拼 16 字节虚拟取指缓冲。
- **失效精度**：invlpg 只擦该虚拟页的解码条目（cache 新增 erase_if，命中才
  bump code_epoch）；CR3 写不再全清解码缓存——CachedInsn 自带 cr3 指纹、命中时
  校验即可（跨进程 vaddr 别名由指纹兜住）。SMC（CPU store + 总线写通知器覆盖
  AHCI DMA）仍是全清。
- 实测：blocks 1.42→2.53 Mips（编译量 38.3k 回常态）；interp 42.4 持平；
  串口 1238=1238；**40M 对拍 26,580,732 边界全绿**；九段全绿。
- 下一战场仍是速度：内存操作数下沉 + 内联页表遍历（桥拷贝主导，blocks 仍慢 17 倍）。

## 2026-11-14 切片加宽 + 桥合并（速度第一轮）

- **切片加宽**（wasm_block.cpp / wasm_block_mem.cpp）：
  - reg/reg 与 reg/imm ALU（add/sub/adc/sbb/and/or/xor/cmp/test）开到 1..8 字节宽，
    统一走 `emit_dst_write`（8/16 位合并高字节、32 位零扩展）+ 带进位语义的
    `emit_flags(kind, top_bit)`（width<8 进位=移出位，与解释器 alu_add 逐位一致）；
  - bt r,r（kI64RemU 取模定位 CF-only 更新）、setcc r8（emit_cc 后**两次 kI32Eqz
    归一化**——emit_cc 返回的是原始掩码位，kBe 直接写会得 2 不是 1）；
  - `mirror_ok` 守卫无 REX 的 h 寄存器字节操作数（AH/CH/DH/BH 是父槽 byte1，
    不在镜像布局上）；REX 后 spl/bpl/sil/dil 是低字节、照常入块。
- **两个实测抓到的正确性坑**：
  - setcc 归一化（上）：lockstep 首个分叉 setbe dl 写 2；
  - **bt reg,imm 混进 ALU 通路**：writes_flags 包含 kBt 但 alu_opcode(bt) 返回 0
    ——0 恰是 wasm `unreachable` 操作码，模块能实例化、运行即 trap（40M 对拍在
    ic 20762799 用户态 rip 0x4c2542 `0f ba e2 17` 抓到）。修法：reg 通路显式
    排除 kBt（只有专用 r,r 分支降它）。
- **桥合并**（src/wasm/bridge.cpp 新 TU）：`mf_bridge_prepare(mask)`=flush+event
  budget、`mf_bridge_finish(rip,retire,mask)`=commit+store-log drain+set_rip+
  retire，每次块执行 8 次 C 跨界降到 3 次；harness 缓存 bridge base 与 DataView。
  顺手修了 bench 的事件预算短路（budget<count 时原来会空转不退休指令，表现为
  “挂死”）。另清掉了 bench 里的 TEMP 诊断（refCodes/refdetail/refbytes）与坏死的
  serialText 字段。
- **拆分**：wasm_block_mem.cpp 772 行超限 → 页表遍历（walk/level/paddr/large
  page/handoff/mode guard）拆到 wasm_block_walk.cpp + wasm_block_internal.hpp。
- **实测（block-bench.mjs，同机同参数）**：
  - 8M：interp 40.40 / blocks **4.45 Mips**（上一轮 2.29，**+94%**）；
  - 40M：interp 25.96 / blocks 1.98 Mips，块执行占比 **45.6%**（1825 万次块执行、
    16.4M 单步；上一轮占比 0.09%）。40M 时变慢的主因是 38k 次逐块
    WebAssembly.instantiate（编译 ~11s）+ 用户态块平均只有 2.8 条指令。
  - 40M lockstep 25,664,798 边界全绿、串口 1238=1238、九段验收全绿。
- 下一战场（按收益）：批量/复用块实例化（instantiate 主导）、call rel32 入块、
  用户态短块合并。

## 2026-11-14 第五轮：调度搬进引擎（mf_runner_*）

JS 调度循环整体移入 `src/wasm/block_runner.cpp`（引擎模块内，247 行）：
缓存探测、epoch 校验、skip 重探（64）、prepare/run/finish、解释器回退
批次（32）全在 wasm 内一次完成，JS 只剩 miss 编译与函数表注册。块入口
通过引擎自身导出的 `__indirect_function_table` 间接调用（`-Wl,--growable-table`
放开上限；低位 431 槽是引擎自己的函数指针，`mf_runner_reset(table_base)`
从表长起分）。registry 64k 槽线性探测，静态数组零分配。

实测（同窗口 40M 交替 A/B，--runner 0/1）：JS 循环 11.37/10.49 →
引擎调度 13.72/13.44 Mips（+27%）；8M 窗口 +45%（12.88-17.41 →
18.39-23.53）。机器当日状态偏慢（interp 26.5），绝对值横向不可比。
串口 956/1238 与解释器一致；40M lockstep 25,664,798 边界全绿；
selftest PASS；九段验收全绿。今日五刀累计（40M 尺度）：1.98 →
13.4-13.7 Mips（同窗口 JS 循环终点 14.10 为快机器时段，引擎调度
按同窗口 A/B 计 +27%）。--runner 0 保留 JS 循环作对照。

## 2026-11-14 第六轮：块终于真的在跑（WP=1 是元凶）

关键发现：内核启动后 CR0.WP=1，而块入口守卫是 `cpl!=0 || WP=1 → handoff`，
所以**所有块都在空转**。40M 里 453 万次块执行有 438 万次 done=0（97%），
块均只退休 0.54 条——此前所有"块性能"数字都只是在量解释器批次加调度开销。

修法与随之暴露的问题：

1. **守卫粒度**：改成"逐次写检查目标页 RW 位"（读不查，WP 只影响写）。
   walk 与 TLB 探测都带上 `write` 参数。
2. **kTmpA 冲突（真 bug）**：walk 里把页表项的 U/S+RW 位暂存在 kTmpA，
   而 RMW（`add [m],reg`）的操作数 A 也在 kTmpA——`add [rax],r13` 的加数
   被换成了 0/1 权限位。锁步在 icount 1030 万处抓到（flags 停在前一条指令上）。
   改存 kTmpCi（carry-in 槽，walk 之后才被 emit_carry_in 写）。
3. **用户态进块**：cpl≠0 不再一律回退，按页表 U/S 位逐次判；越权访问仍交回
   解释器触发真正的 #PF。TLB 条目扩到 24 字节存 U/S|R/W。
4. **块内 TLB**：512 项直接映射，CR3 写/INVLPG 都 bump 一个 generation，
   `mf_bridge_prepare` 见 generation 变化就整表清零。
5. **lea r,[m]** 进块（地址只是编译期常量或 base+index*scale+disp）。

实测（40M，cpuMips 取中位）：blocks 27.4-28.3，interp 40.2-41.6。
仍慢于解释器（约 0.68 倍），但这是块第一次真正参与执行。
40M 逐边界差分锁步（`/tmp/lockstep5.mjs`，真 runner 驱动 vs 纯解释器）
终态全等、串口 1229/1238 一致、九段验收全绿。

jcc fall 路径内联（块均 1.69→3.23 条）已实现但**暂缓**：它有语义 bug
（锁步在 3000 条处就分歧，A 多跑 5 次循环迭代而 icount 相同），
定位到就恢复。下一步：修它、压调度循环 12%、补 call/ret 的块内支持。

## 2026-11-15 收尾：块模式的极限与三次外包的收获

### 最终定位
块模式 32.5 cpuMips，纯解释器 41.5（比值 0.78–0.80）。会话起点是 5.46，
净提升约六倍。**没有超过解释器**，且以下方向全部实测排除：

| 方向 | 结果 |
|---|---|
| 指令族补齐（call/push/pop/ret） | 中性：walk+store-log 开销抵掉省下的 decode/dispatch |
| 调度单条缓存 | 中性（A/B 30.26 vs 30.29） |
| 批次调小提高覆盖率 | 更慢（19.8 → 11.7）：块覆盖率上去但块内单位成本更高 |
| 镜像权威（块间不搬寄存器） | 被两处核心机制挡住：`Cpu::retire` 内部投递中断；`mf_event_budget` 依赖 scheduler 的实时 dispatch |
| jcc fall 路径内联 + 循环展开 | 块执行数 -45%、块均 4.7→8.6，但热点块卡在 SSE（见下） |
| SSE movups 进块（codex 完整实现并调通） | 正确，块数 -4.4%，**吞吐持平** |

### 三个真 bug（都是锁步/差分抓出来的）
1. `lower_memdst_alu` 操作数顺序：`cmp %sil,(%rdi)` 算成 `sil - [rdi]`。
   sub/cmp/sbb 非交换，CF/SF/AF/PF 全反。
2. 块出口脏寄存器集：jcc 早退时 fall 路径的寄存器是未初始化的 local，
   却按整块全集提交。改成两种出口都上报实际脏位。
3. runner 忽略 `kExitDoneSlot`：早退执行 1 条却按静态 16 条退休，
   多出的 15 条正好等于 5 圈 memcpy 的 rdx 差值（0xa0）。
4. **SSE 不退休**（`c006777`）：`execute_sse`/`execute_sse_ext` 成功尾部没有
   `retire(1)`，解释器执行 movups 后不计数就继续，icount 与 RIP 错位。

### 为什么到头了
块内每条指令约 41ns，解释器 24ns。每块固定开销约 190ns（调度 + bridge +
entry/exit 寄存器搬运 + walk），块均 4–5 条摊不薄。加大块长（内联/展开/SSE）
能把块数降一半，但固定开销按块计，省下的 decode/dispatch 抵不过新增的 walk 与搬运。

真正跨过去要块间寄存器驻留（免搬运），而那条路卡在上面两个核心机制上，
需要改 `retire` 的中断语义——影响面远超收益。

### 外包记录
三次 codex 委托（报告分别在 `/tmp/codex-dir-20261114`、`/tmp/codex-jcc`、
`/tmp/codex-sse`）：
- 方向评估：给出短批次回退等排序，最终采纳了 4 条
- jcc 分歧排查：定位 `kExitDoneSlot` 未被消费（本笔记第 3 条）
- SSE 实现：完整调通并按约束保持仓库干净；顺带发现 SSE 不退休的解释器 bug，
  这一条已单独合并（`aeb4081`），SSE 进块本身因吞吐持平未进主干。
