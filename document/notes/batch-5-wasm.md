# 批 5 wasm 引擎产物 — 笔记

范围与验收见 `document/ai/PLAN.md` 批 5 节，方案全文 `document/ai/batch-5-wasm-proposal.md`（维护者审定后落地）。本文记沉淀性的知识：布局铁律的落地、空指针毒与整数基、工具链与门禁的工程账。

## 交付形状

- 产物：`build-wasm/engine.wasm` 约 80 KB，STANDALONE_WASM 单文件、无 JS 胶水、`--no-entry`，配方 `scripts/build-wasm.sh`（em++ 7.0.0，-O2 -fno-exceptions -fno-rtti）。
- 内存布局（规格 5.4 铁律落地）：线性内存偏移 0 即客体物理 0。`[0, 512MiB)` 客体 RAM，`[512MiB, +4MiB)` LFB 预留（几何批 8 定，地址 `mf_lfb_base()` 可查），引擎静态数据与堆在 557842432 起的窗口之上（`--global-base`），`--max-memory=4GiB` 写死换零检查访存。Bus 是路由权威：debug-exit（0xFEED0000）走映射，不占线性内存背书。
- C 导出面（`src/wasm/exports.cpp`，ABI 版本 1）：三个版本查询（ABI/引擎/快照格式——「三元组随产物可查」的落点）、机器生命周期（reset；create 的角色由制品级 RAM 尺寸取代——布局是制品属性不是实例属性）、`mf_run` 返回打包 kind+code、`mf_read_console`、`mf_snapshot`（容量不足返回所需长度）、`mf_restore`。宿主直接经导出的 memory 读写客体内存，没有 load-image 导出（共享线性内存即接口）。
- Machine 装配层（`machine.hpp/.cpp`）：批 4 遗留归属兑现。批 4 的 test_snapshot 手工装配换成 Machine API 后语义不变；设备钩子（快照/恢复）留了批 6 的口。
- 验收：Node 自验（`wasm/harness/selftest.mjs`）断言版本三元组、裸客体退出码 0xAA、console 空、快照长度可查；浏览器同流程 `selftest.html`（交付件，本机无头缺席不阻塞）。acceptance 新增 wasm 段：激活得了 emcc 才跑，否则 SKIP。

## 本批最重要的一课：空指针算术是毒（GEP on null）

第一版窗口模式把线性偏移 0 当指针 `reinterpret_cast<uint8_t*>(0)` 传进去，症状诡异到值得记录：**读全部正常、写全部消失**——取指、单步、镜像回读都对，但 table 写入和栈压入全是零，程序跑成一百万条垃圾指令。机理：LLVM 对 null 常量基的 GEP（`p + off`）在优化时按毒值处理，store 被当死代码删除；读碰巧活下来。修法是 **RamRegion 改整数基**：`base_` 存 `uintptr_t`，每次访问 `reinterpret_cast<uint8_t*>(base_ + offset)`——inttoptr(运行时值) 无法折叠成 null-GEP，wasm32 指针本就是线性偏移，这是诚实形状不是取巧。配套：`data()` 降级为仅自持模式可用，窗口模式走 `copy_in`/`view`（Machine 装镜像、exports 的快照读写全部改道）。host 构建永远测不出这类问题——**窗口路径只有 wasm 自验盖得住，这就是「Node 跑通」作为验收项的全部价值**。

## Reactor 契约与 WASI 桩

- `_initialize` 必须由宿主在实例化后手动调（reactor 模式全局构造函数的入口）。不调的症状：静态 `g_machine` 全零——lfb 报 0、首取指即 fault。两个 harness 都在最前面调它。
- 导入表四件：`env.emscripten_notify_memory_growth`（growth 的通知钩子，空实现）加 WASI 三桩（fd_close/fd_write/fd_seek，reactor 运行时声明、引擎不实际走）。harness 里给了空桩。

## 快照的几何发现（记档，批 6+ 的活）

v0 快照含全量 RAM：541065421 字节的 blob 放不进 541065216 字节的客体系窗口——**region 装不下自己的镜像加头**。自验降级为「长度可查」断言。大 blob 的宿主放置策略（grow-above-top 约定、或压缩后落盘）与设备库一起设计，不在这里即兴。规格 5.5 本来就把压缩与持久化排进 v1。

## 工具链与门禁的工程账

- emsdk 是工具链类目（同 gcc/clang），不是构建依赖：clone 进 `tools/emsdk/`（gitignore，可翻源码），版本钉在 `tools/use-emsdk.sh` 的 `EMSDK_VERSION="7.0.0"`。googletest 仍是唯一的构建依赖。
- 下载三折：本仓直连 storage.googleapis.com 截断挂起；维护者代理拉完整包但落错目录（从终端输出倒推出 `reference/`）；emsdk 只认带哈希前缀的缓存名，裸名包被无视重下。教训：离线包喂 emsdk 要改名成 `<hash>-wasm-binaries.tar.xz`。
- 三道门（format/tidy/doxygen）曾把 emsdk 自家源码吞进扫描，统一改为 **`git ls-files` 取被跟踪文件**——gitignore 的东西天然出局，往 tools/ 塞什么都不用再改门。
- tidy 段曾占总时长大半：clang-tidy 无并行，26 个文件串行每个 6-9 秒。改 `xargs -P$(nproc)` 后全量验收从五六分钟降到 **51 秒**。
- `assert` 里带副作用是自埋雷：本仓构建带 `-DNDEBUG`，`assert(bus_.map(...))` 在正式构建里整个消失、RAM 没映射。Machine 构造函数改为显式判断加 `std::abort()`。
- 批 3 评审轮的 tidy 门这轮又立功：exports.cpp 的 NOLINT 名单补了三个检查名（cert-err58-cpp、bugprone-throwing-static-initialization、misc-const-correctness），`GUEST_RAM_MB` 的分析模式默认值也是被它逼出来的（tidy 无编译数据库条目，#error 会炸段）。

## wasm 侧读数

int-alu 循环（与 host 基准同客体同跳数）：**中位 5,252,615 ips**（11 reps，-O2）。比 host 树的 3.7M 还高——host 基准树是 RelWithDebInfo，wasm 全力 -O2。读数记档不进 `bench/baseline.txt`（host 与 wasm 不混表）；「只许涨不许跌」的守护暂只对 host 表，wasm 基线等批 6 真客体时再立。

## 对抗审查轮（2026-11-08，纪律第 8 条）

三路审查加复核：43 条立案，驳回 0、降级 4、维持 39（复核者七组实测对拍，含原生对照与 wasm 探针）。报告在 `private/review-batch5/`。修复清单：

- **B1（阻断，三报告共指 + 复核双重复现）**：窗口模式 restore 的尺寸校验用持有向量的 `bytes_.size()`（窗口模式恒 0），wasm 制品上 `mf_restore` 对任何合法 blob 必拒——恰是自验降级盖不住的路径。改 `size_`，补窗口模式往返回归测试。
- **write 不推进 rip（复核者旁证新发现）**：`execute_syscall` 不设 `rip_ = rip_after`，write 之后客体原地重执行 syscall。旧测试「恰好通过」是因为重执行时 rax 已是返回值、撞进未知 syscall 分支停机——测试断言与 bug 行为互相掩护。修 + 回归（write 后续指令执行一次、console 恰一份）。
- **S1/F1（应修）**：调度器节短读曾被当「零事件」静默接受、伪造大 count 直接 reserve 崩。协议收紧：`restore_begin`/`restore_read_event` 返回 optional，Machine 按 `reader->remaining()` 给 count 封顶，坏节一律 kTruncated。
- **S2（应修）**：tidy 段对工具自身崩溃不设防——xargs 的 123 无人消费，「崩溃且零 warning 行」的段照绿。管线状态显式消费，崩溃即红。
- **F2/F3/F4（应修）**：ABI 约定（kind 序号、错误 +1、code 语义）从散落注释收进 exports.cpp 单一契约块，七个 `static_assert` 把 ExitStatus 序号焊进编译期——重排枚举即编译错，逼 ABI 版本跟进；`mf_lfb_size` 导出补齐布局查询面，`kLfbSize == 0x400000` 与构建配方互钉；三份 harness 的实例化/桩/_initialize/版本解码统一进 `engine.mjs`（浏览器件留内联并注明同步义务），自验期望全部改由导出推导（零硬编码布局常数）；PASS 文案与实际断言对齐（S3：不再宣称 round-trip）。

复核者的 wasm 探针还顺手证明了布局铁律的实然性：LFB 高地址写读成功、窗口外写被 Bus 拒、data 段全在 557842432 之上——这是批 5 方案里「谁验证了 global-base 生效」一问的首份实验答案。

## 开放项

- 快照大 blob 宿主放置策略（见上，与设备库一起设计）。
- 浏览器自验的自动化：本机无头浏览器缺席时是交付件+人工一次；CI 有浏览器 runner 后入段。
- ips 的 wasm 基线表：批 6 真客体时立。
- exports 的 `mf_run` 返回 u64 需要宿主 BigInt；loupe 侧若嫌烦，ABI v2 可以拆成两个 u32 出口（现在不动）。
