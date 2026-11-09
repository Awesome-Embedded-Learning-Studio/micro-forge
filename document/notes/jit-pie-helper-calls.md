# Native JIT：PIE helper 调用与逐字节布局

五个 helper 模板统一将 `E8 <rel32>`（5 字节）替换为
`49 BB <helper64> 41 FF D3`（10+3=13 字节）。每个模板增加 8 字节。
`call_offset` 现在指向 `movabs r11,imm64` 的 8 字节立即数洞，编译器直接
写入完整 helper 地址。它不再指向 call opcode，也不再计算相对位移或检查 ±2 GiB。
保留 `MAP_32BIT` 页分配策略，但 helper 可位于 PIE 的高地址。

下表偏移从 0 开始，区间均为半开区间。`<x32>` 占 4 字节，`<x64>` 占 8 字节，
所有洞初始全为 0。每行起点等于上一行起点加长度：

- 槽位 mov：REX、opcode、ModRM、SIB 共 4 字节，再加 disp32，故长 8、洞在起点+4。
- `add rsi,disp32`：`48 81 C6` 占 3 字节，再加 disp32，故长 7、洞在起点+3。
- `mov r64,imm64`：REX、opcode 占 2 字节，再加 imm64，故长 10、洞在起点+2。
- `mov r32,imm32`：opcode 占 1 字节，再加 imm32，故长 5、洞在起点+1。
- `call r11`：REX.B=`41`、opcode=`FF`、ModRM=`D3`，长 3，无洞。

## kLoadSlot

| 起点 | 指令 | 编码 | 长度 | 洞 offset/size |
|---:|---|---|---:|---|
| 0 | `push rdi` | `57` | 1 | — |
| 1 | `mov rsi,[rdi+base]` | `48 8B B4 27 <base32>` | 8 | 5/4 |
| 9 | `add rsi,disp32` | `48 81 C6 <disp32>` | 7 | 12/4 |
| 16 | `mov rdi,cpu64` | `48 BF <cpu64>` | 10 | 18/8 |
| 26 | `mov edx,width32` | `BA <width32>` | 5 | 27/4 |
| 31 | `movabs r11,helper64` | `49 BB <helper64>` | 10 | 33/8 |
| 41 | `call r11` | `41 FF D3` | 3 | — |
| 44 | `pop rdi` | `5F` | 1 | — |
| 45 | `mov [rdi+dst],rax` | `48 89 84 27 <dst32>` | 8 | 49/4 |

总长 `1+8+7+10+5+10+3+1+8=53`，`call_offset=31+2=33`，`cpu_offset=16+2=18`。
数据洞 `[5,9)`、`[12,16)`、`[27,31)`、`[49,53)`；重定位洞
`[18,26)`、`[33,41)`。旧 dst32@41 随调用扩长后移 8，变成 49。

## kLoadRip

| 起点 | 指令 | 编码 | 长度 | 洞 offset/size |
|---:|---|---|---:|---|
| 0 | `push rdi` | `57` | 1 | — |
| 1 | `mov rsi,abs64` | `48 BE <abs64>` | 10 | 3/8 |
| 11 | `mov rdi,cpu64` | `48 BF <cpu64>` | 10 | 13/8 |
| 21 | `mov edx,width32` | `BA <width32>` | 5 | 22/4 |
| 26 | `movabs r11,helper64` | `49 BB <helper64>` | 10 | 28/8 |
| 36 | `call r11` | `41 FF D3` | 3 | — |
| 39 | `pop rdi` | `5F` | 1 | — |
| 40 | `mov [rdi+dst],rax` | `48 89 84 27 <dst32>` | 8 | 44/4 |

总长 `1+10+10+5+10+3+1+8=48`，`call_offset=26+2=28`，`cpu_offset=11+2=13`。
数据洞 `[3,11)`、`[22,26)`、`[44,48)`；重定位洞 `[13,21)`、`[28,36)`。
旧 dst32@36 后移 8，变成 44；abs64 仍是客体 `rip+length+disp`。

## kStoreSlot

| 起点 | 指令 | 编码 | 长度 | 洞 offset/size |
|---:|---|---|---:|---|
| 0 | `push rdi` | `57` | 1 | — |
| 1 | `mov rcx,[rdi+src]` | `48 8B 8C 27 <src32>` | 8 | 5/4 |
| 9 | `mov rsi,[rdi+base]` | `48 8B B4 27 <base32>` | 8 | 13/4 |
| 17 | `add rsi,disp32` | `48 81 C6 <disp32>` | 7 | 20/4 |
| 24 | `mov rdi,cpu64` | `48 BF <cpu64>` | 10 | 26/8 |
| 34 | `mov edx,width32` | `BA <width32>` | 5 | 35/4 |
| 39 | `movabs r11,helper64` | `49 BB <helper64>` | 10 | 41/8 |
| 49 | `call r11` | `41 FF D3` | 3 | — |
| 52 | `pop rdi` | `5F` | 1 | — |

总长 `1+8+8+7+10+5+10+3+1=53`，`call_offset=39+2=41`，`cpu_offset=24+2=26`。
数据洞 `[5,9)`、`[13,17)`、`[20,24)`、`[35,39)`；重定位洞 `[26,34)`、`[41,49)`。
所有数据洞均在扩长的调用之前，所以偏移保持原值。

## kAddImmHelper

| 起点 | 指令 | 编码 | 长度 | 洞 offset/size |
|---:|---|---|---:|---|
| 0 | `push rdi` | `57` | 1 | — |
| 1 | `mov esi,slot32` | `BE <slot32>` | 5 | 2/4 |
| 6 | `mov edx,imm32` | `BA <imm32>` | 5 | 7/4 |
| 11 | `mov rdi,cpu64` | `48 BF <cpu64>` | 10 | 13/8 |
| 21 | `movabs r11,helper64` | `49 BB <helper64>` | 10 | 23/8 |
| 31 | `call r11` | `41 FF D3` | 3 | — |
| 34 | `pop rdi` | `5F` | 1 | — |

总长 `1+5+5+10+10+3+1=35`，`call_offset=21+2=23`，`cpu_offset=11+2=13`。
数据洞 `[2,6)`、`[7,11)`；重定位洞 `[13,21)`、`[23,31)`。
数据洞偏移保持原值；该模板仍由 `MICRO_FORGE_JIT_ADD` 单独开启。

## kAddMemSlot

| 起点 | 指令 | 编码 | 长度 | 洞 offset/size |
|---:|---|---|---:|---|
| 0 | `push rdi` | `57` | 1 | — |
| 1 | `mov rdx,[rdi+src]` | `48 8B 94 27 <src32>` | 8 | 5/4 |
| 9 | `mov rsi,[rdi+base]` | `48 8B B4 27 <base32>` | 8 | 13/4 |
| 17 | `add rsi,disp32` | `48 81 C6 <disp32>` | 7 | 20/4 |
| 24 | `mov rdi,cpu64` | `48 BF <cpu64>` | 10 | 26/8 |
| 34 | `movabs r11,helper64` | `49 BB <helper64>` | 10 | 36/8 |
| 44 | `call r11` | `41 FF D3` | 3 | — |
| 47 | `pop rdi` | `5F` | 1 | — |

总长 `1+8+8+7+10+10+3+1=48`，`call_offset=34+2=36`，`cpu_offset=24+2=26`。
数据洞 `[5,9)`、`[13,17)`、`[20,24)`；重定位洞 `[26,34)`、`[36,44)`。
仍只登记三个数据补丁，Cpu* 和 helper 地址由编译器各自回填一次。

## 布局断言与 ABI

`has_valid_layout()` 保留 helper/Cpu* 配对、零洞、范围和不重叠检查。
对 helper 洞，先确认 `call_offset>=2`，再用减法检查后续至少有 11 字节，
检查前缀 `[call_offset-2,call_offset)` 为 `49 BB`，以及洞后的三字节为
`41 FF D3`，最后占用完整 8 字节洞。Cpu* 的 `48 BF` 前缀及 8 字节检查不变。
上列所有洞均互不重叠且位于各自模板范围内，不能重复登记为数据补丁。

宿主 `r11` 是 SysV caller-saved 暂存器；客体 r11 保存在 `regs_` 槽中。
函数参数、返回值和 `push rdi`/`pop rdi` 的栈对齐保持原样。无 helper 的模板不变。
最长模板为 53 字节，64 条指令加块末尾 ret 最多占 `64*53+1=3393` 字节，
仍小于 4096 字节页；此次扩长不会触及现有 64 条指令块上限。

## 验证

本次使用现有 RelWithDebInfo 构建（`-O2 -g -DNDEBUG`），内核为
`/home/charliechen/Cinux/build/kernel/kernel.bin`，256 MiB RAM、未挂磁盘，
`MICRO_FORGE_JIT=1`，清除 `MICRO_FORGE_JIT_ADD` 和 `MICRO_FORGE_JIT_TRACE`。
计时范围仍只包含 `Cpu::run()`。没有修改块上限、探针或已有测试期望。

```sh
cmake --build build -j6
ctest --test-dir build --output-on-failure

# CMake 目标继续保留非 PIE，另用同一库和同一探针源码临时链接 PIE。
g++ -std=c++23 -O2 -g -DNDEBUG -fPIE -pie \
  -Iinclude -Ibuild/generated bench/jit_kernel_probe.cpp \
  build/libmicro_forge.a -o /tmp/mf-jit-kernel-probe-pie
readelf -h /tmp/mf-jit-kernel-probe-pie

env -u MICRO_FORGE_JIT_ADD -u MICRO_FORGE_JIT_TRACE MICRO_FORGE_JIT=1 \
  build/bench/mf-jit-kernel-probe \
  /home/charliechen/Cinux/build/kernel/kernel.bin /tmp/jit-abs64-nonpie.console
env -u MICRO_FORGE_JIT_ADD -u MICRO_FORGE_JIT_TRACE MICRO_FORGE_JIT=1 \
  /tmp/mf-jit-kernel-probe-pie \
  /home/charliechen/Cinux/build/kernel/kernel.bin /tmp/jit-abs64-pie.console
cmp /tmp/jit-abs64-nonpie.console /tmp/jit-abs64-pie.console

scripts/build-wasm.sh
```

`readelf` 确认 CMake 探针为 `EXEC`，临时 PIE 探针为 `DYN`。
修改前，先对相同内核复现了非 PIE 33.0414% / 64.774 Mips，PIE 2.2677%。
修改后独立交替执行三轮；两种链接方式每轮均得到相同计数：

| 模式 | 检查点 icount | console 字节 | blocks / failed | JIT 指令数 | 动态覆盖率 |
|---|---:|---:|---:|---:|---:|
| 非 PIE / PIE | 100,000,000 | 1,241 | 9,385 / 12,850 | 41,877,805 | 41.8778% |
| 非 PIE / PIE | 168,855,966（Halt） | 1,873 | 9,665 / 13,218 | 55,792,332 | 33.0414% |

| 轮次 | 非 PIE：100M / Halt Mips | PIE：100M / Halt Mips |
|---:|---:|---:|
| 1 | 69.918 / 66.987 | 68.253 / 68.707 |
| 2 | 65.215 / 66.718 | 70.116 / 68.901 |
| 3 | 67.005 / 66.718 | 68.451 / 67.411 |

Halt 吞吐中位数：非 PIE **66.718 Mips**，PIE **68.707 Mips**。
PIE 的 JIT 指令数从 3,829,180 恢复为 55,792,332，与非 PIE 完全相同。
最终 RIP 都是 `0xffffffff80201ea1`。六份新 console、修改前两种链接方式的
console 和本次解释器 console 均逐字节一致，SHA-256 为
`9620d4f8ac4ba143911a92d0c4a2ac416d4cfa25ed579048f8c132c7b335bad0`。
无磁盘时仍是原有 panic/halt 路径，不代表启动到了 shell。

native 构建通过。现有 200 项 ctest 中 **199 项通过、1 项失败**：
`JitLayoutTest.AddMemHasValidFortyByteLayout` 写死了总长 40、旧 call_offset=34、
`E8` 和 4 字节 rel32 洞，与本次要求的总长 48、call_offset=36、`49 BB` 和
8 字节 helper 地址洞直接冲突。遵守“不改已有测试期望”的约束，未改该测试，
也未跳过或放宽断言；要达到 200/200，需要明确允许同步这项机器码布局契约。
其他执行差分、拒收检查均通过。五个模板另经 `objdump -b binary -m i386:x86-64`
反汇编核对，指令边界与本文一致。

另将**未修改**的 `tests/test_jit_diff.cpp` 临时编译为 PIE，运行全部 20 个执行差分
用例（包括五种 helper 模板），**20/20 通过**；验证 GPR、六标志、内存和实际 JIT
执行计数。复现命令：

```sh
g++ -std=c++23 -O2 -g -DNDEBUG -fPIE -pie -Iinclude -Ibuild/generated \
  -isystem build/_deps/googletest-src/googletest/include \
  tests/test_jit_diff.cpp build/libmicro_forge.a \
  build/lib/libgtest_main.a build/lib/libgtest.a -pthread -o /tmp/test-jit-diff-pie
/tmp/test-jit-diff-pie --gtest_filter='Native/*'
```

`scripts/build-wasm.sh` 返回 0，生成 **475,735 字节**的 `build-wasm/engine.wasm`。
guest window 和 engine base 不变；构建中仍有现有 IoApic/Lapic 构造器字段初始化警告。
wasm emitter 与 wasm block 路径未改动。
