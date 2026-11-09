# wasm 解释器优化实测（2026-11-09）

这是第一轮记录。后续访存与分派优化见 [wasm-hotpaths.md](wasm-hotpaths.md)。

本轮保留三项改动：默认开启 LLVM LTO、给 decode cache 使用轻量地址哈希、
去掉 cache 命中时重复执行的代码页标记。没有启用 guest JIT。

## 测量口径

- AMD Ryzen 7 9700X，Linux，Node v26.12.0，Emscripten 7.0.0，`-O3`。
- guest RAM 256 MiB，运行真实 Cinux kernel.bin 和 ext2.img。
- 每个样本启动独立 Node 进程和 wasm 实例，执行 4 亿条 guest 指令。
- 每次 `mf_run` 的预算为 200 万条，与页面分段调用接近；末段取剩余预算。
- 只计运行阶段；镜像读取、wasm 实例化、boot shim、校验哈希在计时区间外。
  计时包含执行期间 V8 的编译开销。
- 固定 CPU 2，五轮交替正序/逆序测试，用耗时中位数计算吞吐。
- 比较每个样本的实际 icount、退出状态、RIP、串口发送数、控制台 SHA-256、
  完整 guest RAM 与 LFB SHA-256；不同则 benchmark 直接失败。

五轮中位数：

| 构建 | 4 亿条耗时 | 吞吐 |
| --- | ---: | ---: |
| 原始发布构建 | 11.022s | 36.29 Mips |
| 三项优化后 | 7.484s | 53.45 Mips |

吞吐提高 **47.3%（1.473 倍）**，耗时减少 **32.1%**。旧版样本范围
10.385–14.169s，新版为 7.370–7.626s；宿主负载仍存在波动。
结果与原始样本、镜像及产物哈希见同目录 `wasm-interpreter-perf-samples.json`。
测量构建基于 `8903332` 与本轮三项优化。
本轮发布前的 `engine.wasm` 备份在 `build-wasm/engine-before-opt.wasm`，
当轮优化产物归档在 `build-wasm/engine-after-round1.wasm`；构建产物不纳入 Git。
页面的 `example/wasm/cinux/public/engine.wasm` 随后已更新为第二轮测量产物。
文件从 452,283 字节变为 470,627 字节，增加约 4.1%。

## 修改原因

### decode cache 的哈希不是免费操作

wasm32 下，工具链 libc++ 的 `std::hash<uint64_t>` 会调用 `__hash_memory`。
cache 即便命中，也要逐条指令计算通用字节哈希。初始 3 亿条指令的
Node CPU profile 中，该函数约占 9% 的采样。

decode cache 只需要快速选择 set，完整 64 位 RIP 仍用于 tag 比较。
新的 `DecodeHash` 把地址高低 32 位折叠，再把页号折叠到低位，避免通用哈希调用，
也避免只取页内偏移造成不同代码页相同偏移的集中冲突。
优化后的同口径 profile 中，`__hash_memory` 已不再是热点。

### 用 LTO 优化跨文件的执行链

原构建每个 translation unit 独立优化，CPU、MMU、Bus、Scheduler、操作数和
ALU helper 跨文件调用较多。新增 `-flto`，让 LLVM 在链接时一起优化这些代码。
构建仍使用 `-O3 -fno-exceptions -fno-rtti`。

`WASM_LTO=0` 可关闭 LTO 做对照；`WASM_PROFILE=1` 保留函数名供 V8 profiler 使用；
`OUT=...` 可并存多个实验产物。计时使用普通发布产物，profile 用于定位热点。

### 已缓存指令无需重复标记代码页

decode cache 插入前已经标记了代码页。`note_store()` 遇到代码页写入时，
同时清空 decode cache 和代码页 bitmap。因此，只要一条缓存指令仍有效，
它的代码页标记就仍有效。

命中路径现在省去 bitmap 的索引、边界检查和读改写，cache 只保存 `DecodedInsn`，
不再附带仅为重复标记服务的物理地址。新增回归用例先命中缓存、再通过 guest
store 改写指令立即数，连续重复两轮，验证清缓存之后能重新建立追踪。

## 舍弃的实验

把 Scheduler 未到期的比较移入头文件，试图省掉每指令一次的 wasm 调用。
三轮对照中，中位数从 6.431s 变为 7.177s，收益未得到验证，源码已恢复。
这组试验受宿主负载波动影响，不能据此推断内联普遍更慢。

早期构建与 benchmark 同时运行时出现明显耗时波动，早期计时仅用于筛选方向。
最终数据使用完成构建后的独立复测。

## 下一轮优先方向

优化后 3 亿条指令的 profile 中，`execute_insn` 约 27%、主循环约 22%，
`load`/`store` 合计约 17%、MMU 约 8%、decode/fetch 合计约 13%。
这些是采样归因，内联会改变归属；它们不是可以直接相加的独立加速承诺。

1. 为常见 mov、ALU、条件跳转预解码执行信息，减少通用操作数解析与重复分派。
2. 测量 RAM 访存的调用链，评估缓存 host 指针和按宽度特化的快速路径。
   实现时要保留页权限、缺页、MMIO、越界和自修改代码行为。
3. 记录新哈希下的命中率与 set 冲突，再决定 cache 容量和关联度。
4. 需要更大幅度提升时，再接通已有 wasm block emitter 与实际 CPU 状态、
   内存、flags、事件时间和退出恢复。当前调试导出的独立 wasm block
   不能代表完整 guest 执行路径已经具备 JIT。

## 复现

本工作区已保留这一轮的前后构建，可直接比较。如果工作区已是优化源码，
关闭 LTO 只能隔离 LTO，不能还原旧版地址哈希和代码页标记。

```bash
taskset -c 2 python3 wasm/harness/cinux-bench.py \
  build-wasm/engine-before-opt.wasm build-wasm/engine-after-round1.wasm \
  --kernel /path/to/kernel.bin --disk /path/to/ext2.img --reps 5
```

采样定位热点：

```bash
WASM_PROFILE=1 OUT=build-wasm/engine-prof.wasm bash scripts/build-wasm.sh
node --cpu-prof wasm/harness/cinux-bench.mjs build-wasm/engine-prof.wasm \
  --kernel /path/to/kernel.bin --disk /path/to/ext2.img
```

## 验证

测量时构建通过 182 项原生测试和 182 项 ASan/UBSan 测试，包含自修改代码用例；
沙箱环境下使用 `ASAN_OPTIONS=detect_leaks=0`，未验证泄漏检测。
工作区后续包含并行的 native JIT 修改，最终原生复查为 192/192 通过。
发布 wasm 通过 ABI/bare-client 自测，并在 4 亿条指令内输出 `Cinux up`。
修改的 C++ 文件通过 clang-format、clang-tidy，行数与 Doxygen 检查通过。

固定指令数的 Node/V8 结果用于比较引擎吞吐。页面交互延迟和真实浏览器吞吐
需要在对应浏览器、分段预算与运行策略下单独测量。
