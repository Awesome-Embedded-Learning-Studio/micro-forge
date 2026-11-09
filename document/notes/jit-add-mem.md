# `add [base+disp],reg` 原生 JIT 模板与验证

下文为最初实现及当时测量的历史记录（40 字节 rel32 模板、4096 块上限）。
当前块上限已为 32768，五个 helper 模板已改用 64 位绝对地址间接调用；
现行逐字节布局及 PIE 修复验证见 [jit-pie-helper-calls.md](jit-pie-helper-calls.md)。

本次支持 64 位、无 index、非 RIP-relative 的 base+disp 内存 ADD。
FS/GS、LOCK、其他操作数宽度和 32 位地址形式继续交给解释器。
模板不受 `MICRO_FORGE_JIT_ADD` 控制；只需在进程启动前设置
`MICRO_FORGE_JIT=1`。`MICRO_FORGE_JIT_ADD` 仍只控制原有的寄存器立即数 ADD。

## 字节布局

偏移从 0 开始，每行起点等于上一行起点加长度。

| 起点 | 指令 | 编码 | 长度 | 洞 offset/size |
|---:|---|---|---:|---|
| 0 | `push rdi` | `57` | 1 | — |
| 1 | `mov rdx,[rdi+src]` | `48 8B 94 27 <src32>` | 8 | 5/4 |
| 9 | `mov rsi,[rdi+base]` | `48 8B B4 27 <base32>` | 8 | 13/4 |
| 17 | `add rsi,disp32` | `48 81 C6 <disp32>` | 7 | 20/4 |
| 24 | `mov rdi,cpu64` | `48 BF <cpu64>` | 10 | 26/8 |
| 34 | `call jit_add_mem` | `E8 <rel32>` | 5 | 35/4 |
| 39 | `pop rdi` | `5F` | 1 | — |

总长 `1+8+8+7+10+5+1=40`。`cpu_offset=26`；`call_offset=34`
指向 E8 opcode，调用位移洞从 35 开始。

三个数据补丁仅为 `(5,4)`、`(13,4)`、`(20,4)`；Cpu* 和 rel32 由
`compile_block()` 单独重定位。五个半开区间依次是 `[5,9)`、`[13,17)`、
`[20,24)`、`[26,34)`、`[35,39)`，互不重叠且都在 `[0,40)` 中，
初始字节全为 0。`code[24:26]` 为 `48 BF`，`code[34]` 为 `E8`。

原失败探针 `/tmp/add_mem_probe` 的字节数组、26 和 34 两个偏移都正确。
对旧二进制的 `translate_insn()` 反汇编显示，其补丁表还有第四项
`{offset=26,size=8,value=0}`，与 `cpu_offset` 重复占用同一区间。
因此 `has_valid_layout()` 正确拒绝它。当前实现只登记三个数据补丁，
没有修改或放宽布局检查；回归测试保留了重复登记 Cpu* 洞的失败案例。

helper 的 SysV 参数为 `rdi=Cpu*`、`rsi=vaddr`、`rdx=value`。
它通过 `jit_load`/`jit_store` 做 qword 读改写，并复用 `alu_add` 更新六个标志。
新增差分用例发现解释器的 ADD AF 原取了 bit 3，现改为
`(a ^ b ^ sum) & 0x10`，表示从 bit 3 到 bit 4 的进位。
`7+1` 必须清 AF，`15+1` 必须置 AF；原有测试期望值没有修改。

## 可复现验证

```sh
cmake --build build -j6
ctest --test-dir build --output-on-failure

env -u MICRO_FORGE_JIT_ADD -u MICRO_FORGE_JIT_TRACE MICRO_FORGE_JIT=1 \
  build/bench/mf-jit-kernel-probe \
  /home/charliechen/Cinux/build/kernel/kernel.bin /tmp/addmem-jit.console

env -u MICRO_FORGE_JIT -u MICRO_FORGE_JIT_ADD -u MICRO_FORGE_JIT_TRACE \
  build/bench/mf-jit-kernel-probe \
  /home/charliechen/Cinux/build/kernel/kernel.bin /tmp/addmem-interpreter.console

cmp /tmp/addmem-jit.console /tmp/addmem-interpreter.console
```

构建通过，ctest **192/192** 全绿。其中新增 8 项执行差分和 2 项布局/拒收测试。
执行差分强制二次访问，断言恰好一条客体指令实际经过 JIT；检查全部 GPR、
六标志、目标内存及相邻字节，覆盖正负位移、无位移、高位源寄存器值、
base/source 别名、CF/OF/AF/PF 等边界。

探针使用 RelWithDebInfo（`-O2 -g -DNDEBUG`），非 PIE 链接。
计时仅包围 `Cpu::run()`，不包含机器构造、文件加载、boot shim 和 console 写盘。
环境为 256 MiB RAM、默认 Cinux 设备、未挂磁盘。此次单次顺序对照结果：

| 模式 | halt 时 icount | `jit_insn_count()` | 动态覆盖率 | `Cpu::run` Mips |
|---|---:|---:|---:|---:|
| 解释器 | 168,855,966 | 0 | 0% | 46.462 |
| 不含 add-mem 的对照 | 168,855,966 | 43,691,543 | 25.8750% | 45.867 |
| 修复后的默认 JIT | 168,855,966 | 43,692,566 | 25.8756% | 45.658 |
| JIT + 原有 ADD immediate 开关 | 168,855,966 | 43,729,049 | 25.8972% | 49.147 |

不含 add-mem 的对照通过临时编译一份移除新增 lowering 分支的翻译器对象实现；
其他库对象保持一致。吞吐为本机单次实测，不能据此声称 add-mem 带来性能提升。

默认 JIT 在累计 100,000,000 条时：console **1,241 字节**，
JIT 41,816,253 条，41.8163%，50.524 Mips。该点仍为 Running。
最终 **Halt**，RIP `0xffffffff80201ea1`，console **1,873 字节**。
六种对照模式的最终 console 逐字节相同，SHA-256：
`9620d4f8ac4ba143911a92d0c4a2ac416d4cfa25ed579048f8c132c7b335bad0`。
此无磁盘场景最终输出 `ext2 volume refused to mount`，属于与解释器一致的
panic/halt 验证，不代表已启动到 shell。

## 为什么覆盖率没有到 50%

### 1. 全文件静态分布把数据解成了指令

同一份 kernel.bin 的线性扫描恰好得到 **304,261** 条，但其中的内存 ADD
绝大多数并非 64 位代码。新增探针可按操作数宽度复查：

```sh
build/bench/mf-jit-kernel-probe --profile /home/charliechen/Cinux/build/kernel/kernel.bin
objcopy -O binary --only-section=.text \
  /home/charliechen/Cinux/build/kernel/cinux_kernel /tmp/addmem-text.bin
build/bench/mf-jit-kernel-probe --profile /tmp/addmem-text.bin
```

ELF `.text` 的 LMA 为 `0x200030`，image load_paddr 为 `0x200000`，
所以对应文件偏移 `0x30`，长度 `0x8a6e0`；已逐字节确认两个来源一致。

| 范围 | 解出的指令 | `add [mem],reg` 8/16/32/64 位条数 | `00 00` 条数 |
|---|---:|---|---:|
| 整个 kernel.bin | 304,261 | 92,867 / 0 / 382 / 28 | 90,312 |
| ELF `.text` | 146,131 | 32 / 0 / 2 / 28 | 28 |

`00 00` 是 `add byte [rax],al`；其中 90,284 条落在 `.text` 之外。
这解释了 30.7% 的异常大头，不能把它直接加到动态 JIT 覆盖率上。
40 字节模板覆盖的是 28 条 qword ADD。保持块上限时，实际只多执行了
**1,023** 条 JIT 指令。

### 2. 4096 个块的上限已经用满

`maybe_compile_block()` 对每个首次解码的 RIP 尝试编译，可能产生相互重叠的
后缀块；达到 4096 后，不再编译后续地址。
在 `/tmp` 编译唯一改动为 `max_blocks=32768` 的 `jit_cpu.cpp` 对照，
实际生成 **9,665** 块，halt 时 JIT **55,792,332** 条，覆盖率 **33.0414%**，
47.838 Mips，console 保持一致。仓库默认上限未改。

### 3. PIE 下 helper 超出 call rel32 的距离

`JitPage::create()` 用 `MAP_32BIT` 分配低地址页，`compile_block()` 只接受
有符号 32 位相对调用距离。PIE 的 helper 常位于 `0x55...`，超距会放弃整个块。
现有 `mf-cinux-play` 是 PIE；差分测试和新探针显式使用 `-no-pie`。

用完全相同探针对象临时链接 PIE 版本，halt 时 JIT 只有 **3,829,180** 条，
覆盖率 **2.2677%**，40.404 Mips，console 仍一致。
要支持一般 PIE 宿主，需要近地址 trampoline 或间接调用模板；
本次保留现有模板 ABI 和距离检查。

静态分布修正后，应使用动态热点数据决定后续模板优先级；jcc、call、
push/pop 和其他算术形式仍会切断编译块。
