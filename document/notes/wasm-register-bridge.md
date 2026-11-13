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
