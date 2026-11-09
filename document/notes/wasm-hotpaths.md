# wasm 访存与指令分派优化（2026-11-09）

本轮基线是开工时重新构建的工作区，已包含上一轮的 LTO、decode cache 地址哈希、
去除重复代码页标记，以及已有 native JIT 工作。Cinux 跑的是 wasm 解释器。
本轮缩短 TLB 命中、普通 RAM 访问和常见已解码指令的执行路径。

## 访存路径

`Mmu::lookup()` 是可内联的、无副作用的命中查询。它检查 VPN tag、当前 CPL、
写权限与 CR0.WP、取指权限与 EFER.NXE，成功时通过引用直接返回物理地址。
未命中或权限不满足时，继续使用 `translate_access()` / `Mmu::translate()`，
由原路径执行页表遍历、生成错误码、记录 CR2、注入异常和恢复执行位置。

普通 RAM 通过 `Bus::map_ram()` 显式登记稳定的整数 backing base。
`Bus::direct_ram()` 验证整个访问同时位于映射和 backing 范围内，随后 CPU
按宽度执行小端读写，省去成功路径上的 Bus 查找、Region 虚调用和结果对象传递。
写入仍执行 `note_store()`，保留代码缓存失效处理。

`map()` 保持原有虚访问行为；有访问回调的 RAM 子类也使用它。
这使现有扩展不会因为继承 RamRegion 而自动绕过回调。MMIO 继续走宽度访问方法。
整数 base 可以为 0，匹配 wasm 物理地址 0；快速查询用 bool 表示命中。

这次优化复用原有单地址翻译的访问语义；没有扩展跨 guest 页访问的模拟范围。
跨映射或超出 backing 的访问会退出 RAM 快速路径。

## 指令分派

decode cache 的每个 `CachedInsn` 增加一个 `ExecOp`。只在插入时检查操作数形式，
重复访问直接进入 `execute_cached()` 的小 switch。

覆盖常见 32/64 位 MOV、窄内存零扩展读取、各宽度 store、立即数 store、LEA、
64 位寄存器目的 ALU、INC/DEC、直接跳转和条件跳转。完整 DecodedInsn 仍保留。
高字节寄存器别名、CR/DR、SSE、字符串、syscall 等形式继续调用完整执行器。

执行计划只选择现有语义的实现路径。每指令退休、事件调度、trace、flags、
FS/GS、RIP 相对寻址和故障退出位置保持一致。一次 MMIO 读取可能触发回调，
因此 ALU 必须先捕获目的寄存器，再读取内存源；该顺序有独立回归测试。

## 测量

使用与上一轮相同的 Node v26.12.0、Emscripten 7.0.0、`-O3 -flto`、
256 MiB guest RAM、真实 Cinux 镜像。CPU 固定为逻辑核 2。
每个样本用独立 Node 进程运行 4 亿条指令，每段预算 200 万条。
五轮交替执行顺序，计时前完成构建；镜像装载和校验在计时区间之外。

每轮比较完整 guest RAM/LFB 哈希、控制台哈希、RIP、串口计数、icount 和退出状态。
最终数据及产物、镜像哈希保存在 `wasm-hotpaths-samples.json`。

| 构建 | 4 亿条耗时中位数 | 吞吐 |
| --- | ---: | ---: |
| 本轮基线（包含上一轮优化） | 7.442s | 53.75 Mips |
| 本轮优化后 | 5.213s | 76.73 Mips |

吞吐提高 **42.8%（1.428 倍）**，耗时减少 **29.9%**。五次耗时范围分别为
7.356–8.916s 和 5.017–5.490s。发布产物从 471,910 增至 475,722 字节，增加约 0.8%。
`build-wasm/engine.wasm` 与页面 `public/engine.wasm` 已同步到实测产物。

早期三轮筛选中，TLB 命中优化从约 7.20s 到 6.82s，增加 RAM 直接访问到约
6.25s；随后加入预解码执行从约 6.32s 到 5.18s。早期实验用于筛选方向，
最终结论使用完整改动后的五轮对照。

另用现有 `ips.mjs` 测两个循环，每次 400 万次循环，预热一次、测量 11 次：

| 场景 | 本轮基线 | 优化后 | 吞吐倍数 |
| --- | ---: | ---: | ---: |
| int-alu | 101.67 Mips | 177.09 Mips | 1.74 |
| mem-mix | 81.11 Mips | 171.33 Mips | 2.11 |

同口径 CPU profile 中，MMU translate 的采样占比从约 7.5% 降至 1.8%，
通用 `execute_insn` 从约 27.4% 降至 8.3%。常见形式的执行代码已被内联进
`mf_run`，该函数占比会相应上升，不能把这些比例差直接当作独立加速率。

## 验证

- 200/200 原生测试和 200/200 ASan/UBSan 测试通过。
- 缓存启用/关闭的 29 组指令序列逐次退休比较全部 GPR、flags、RIP、icount、
  内存和调度事件时间；第二遍运行确保命中缓存。
- 单独验证 MMIO 回调的操作数读取顺序、故障 RIP 与不退休行为。
- 验证权限动态变化、INVLPG、flush、paging 开关、RAM 映射与 backing 边界，
  以及普通 RAM 子类的回调仍被保留。
- wasm 自测额外通过 guest 指令读写物理地址 0，并重复执行覆盖缓存命中。
- sanitizer 在本沙箱中设置 `ASAN_OPTIONS=detect_leaks=0`，不包含泄漏检测。
- 修改的 C++ 通过 clang-format、clang-tidy、Doxygen 和 600 行限制检查。

## 复测

本轮旧产物保留为 `build-wasm/engine-before-hotpaths.wasm`。

```bash
bash scripts/build-wasm.sh
taskset -c 2 python3 wasm/harness/cinux-bench.py \
  build-wasm/engine-before-hotpaths.wasm build-wasm/engine.wasm \
  --kernel /path/to/kernel.bin --disk /path/to/ext2.img --reps 5
node wasm/harness/selftest.mjs build-wasm/engine.wasm
```

该结果衡量 Node/V8 中引擎吞吐；浏览器交互响应需要在对应运行环境中测量。
