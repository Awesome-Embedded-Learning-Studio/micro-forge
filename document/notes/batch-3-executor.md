# 批 3 执行器最小闭环 — 笔记

范围与验收见 `document/ai/PLAN.md` 批 3 节。本文记沉淀性的知识：执行器形状、被执行暴露的三个解码器漏洞、乘除法的可移植写法、基准方法与画像方法。

## 执行器形状（后续批次直接沿用）

- `Cpu` 只认 `Bus` 与 `Scheduler`：取指与数据都走总线，长模式平段模型（es/cs/ss/ds 覆盖前缀不改变寻址，fs/gs 段基址未建模、访存直接报 kUnsupported 而非拿基址 0 猜）。
- `ExitStatus` 单向闩锁：kHalt / kExit（syscall exit，code=rdi）/ kDebugExit（(val<<1)|1）/ kDecodeError / kUnsupported（detail=助记符序号或 syscall 号）/ kFault（detail=FaultId，code=故障地址）。闩住后 step/run 复读同一状态直到 reset。出圈指令带 RIP 报告，绝不静默猜测——规格第 6 节在解码层的镜像，执行层同一条纪律。
- icount 语义：rep 字符串指令每次迭代计一条（规格 5.2「微操作逐条计数」），每次计数后 `scheduler.dispatch(icount)`，与批 1 FakeCpu 的契约一致。rep 在批 3 内不会被中断源打断（还没有中断线），预算在指令边界生效。
- 子寄存器写规则集中在 `write_gpr`：32 位写零扩展、16/8 位写保留高位、无 REX 时编码 4-7 号是 ah/ch/dh/bh（宿主寄存器右移 8 位）。读写都走这一个函数，别在指令处理里手拆。
- `DebugExitRegion` 是地址无关的（挂哪由客体配置定），回调进 `Cpu::request_debug_exit`。建议基址 0xFEED0000（IO-APIC 0xFEC00000 与 LAPIC 0xFEE00000 之间没人要）。退出编码照抄 QEMU isa-debug-exit，将来端口 IO 版前挂同一个约定，Cinux 测试桩不用改判退出码的逻辑。

## 被执行暴露的三个解码器漏洞（差分全绿也拦不住的那种）

1. objcopy 拍平裸客体必须 `-j .text`：endbr64 让 as 自动发 `.note.gnu.property`（CET 属性段），不带段过滤时它被拍进 flat bin 的开头，`_start` 前十几条指令根本不在文件里。批 2 的解码门只查「逐字节 100% 解出」，note 字节恰好条条能解，于是语料悄悄错了整整一批。教训：**验收语料要做内容断言（指令数、退出码），纯通过率断言对「内容错了但没报错」的语料零防御。**
2. A0-A3 moffs 操作数退化为立即数：Slot::kMem 在无 ModRM 时没人把 `kind` 设成 kMem，尾部立即数路径顺手把它标成了 kImm，`mov rax,[0xDEAD0000]` 变成 `mov rax,0xDEAD0000`，访存整个消失。差分没抓到是因为归一化层恰好把 objdump 的 moffs 写法和我方的立即数写法拉平了。教训：**文本差分测的是渲染一致性，操作数语义（访存还是立即）必须靠执行或专门的 IR 断言。**
3. 0F A5/AC/AD 表项错位：A5（SHLD 的 CL 形式）缺失、AC 被标成 SHLD 带 CL，而按手册 AC/AD 是 SHRD 的 imm8/CL 两种形式。kernel.bin 全程未命中这三条所以差分零发现。执行器实现 shld/shrd 时才对上。已修，kernel.bin 差分回归 mismatch 0 不变。

## 乘除法的可移植写法（wasm32 没有原生 128 位整数）

- 64×64→128 用 32 位半字分解（`mul_wide`），有符号高半用补码恒等式修正（`imul_high`）。全程 uint64 运算，无 UB，wasm32 直接可编译。
- 有符号除法先把 `INT64_MIN / -1`（UB）单独拦成 #DE，其余 `int64` 除法安全；商超宽判溢出。
- alu_add 的进位在宽度 <8 时用 `(sum >> 8*width) != 0`（u64 域里看得见操作数域的进位），宽度 8 才用 `sum < a` 的回绕判法。初版只写了回绕判法，8 位加法的 CF 全丢——单测矩阵（0xFF+1 之类）当场抓住。
- inc/dec 保 CF：先存后还。neg 的 CF 直接用 alu_sub(0, x) 的借位（非零即借）。

## 内核这批长出来的部分

- Region/Bus 补齐 64 位读写（push/pop、mov r64 的执行需要）。批 1 笔记说「第二个消费者出现时升级」，执行器就是那个消费者。
- le_access 吸收了 put_le16/32/64（RAM 不再私藏 put 助手）。
- `kernel/bit.hpp`：位域词汇（ones / mask_bit / extract / deposit），形状借自 Cinux base 的 bitmask.hpp（同维护者，MIT）。动机是评审意见：子寄存器访问（ah 的读、16/8 位部分写、mul/div 的 AX 商余组合）原先裸写 `~0xFF00ULL` 一类字面量，正是「位置到值的翻译进了人脑」。两仓各自持有一份小拷贝，不跨仓 include。ModRM/SIB 字段位置在 internal.hpp 起名一次（kModrmMod/Reg/Rm、kSibScale/Index/Base），解码器的抽取点全部按名读。标志位公式（AF/OF）是算术恒等式不是字段布局，维持原样。
- make_rm 从 decoder.cpp 挪进 tables.cpp 的 `resolve_rm`（decoder.cpp 顶 600 行线限，拆的是它因为解析逻辑跟表是一对）。

## 基准方法与首份读数

- `bench/mf-bench`：客体循环小程序（GNU as 现场汇编，-j .text），跳数从 0x80000 的循环单元读入，宿主不重汇编就能调 workload。ips = 退休指令数 / 墙钟。热身 80 万次、测量 800 万次、11 次取中位数，RelWithDebInfo + taskset 锁核，值取 median×0.85 存 `bench/baseline.txt`，容差 15%，advisory（不硬失败，v1 先例）。
- 首份读数：int-alu ~3.7M ips、mem-mix ~3.7M ips。朴素解释器的真实水平，取指每指令 16 次 Bus 字节读是已知大头（每次都是 resolve 加虚调用）。按纪律 9 不预设提速；真要动时第一杠杆是 bulk fetch（Region 一次给一段），基准门守底只许涨。

## .text 精确画像方法

- `scripts/dec/text_profile.py <kernel.bin> <ELF>`：ELF 的可执行段（.text*/.init*，readelf AX 标志）取 LMA（objdump -h；内核链接在 0xffffffff80... 的虚拟地址上，VMA 对拍平镜像没用），相对 load_paddr 映射回 kernel.bin 文件偏移，**逐字节与 ELF 段内容比对自校验**，对不上就拒绝输出。
- 一个反直觉事实：kernel.bin 的 ImageHeader 不是前置的工具头，它本身就是镜像内第一个段（.image_header，LMA == load_paddr），所以映射公式是 `file_off = lma - load_paddr`，**不加** header_size。初版加了，自校验当场拦下。
- 结果（document/notes/batch-3-text-profile.txt）：.text 146129 条解出、18 个失败位点（15 个白名单外 + 3 个坏 modrm，集中在 0x814xx-0x89xxx，疑为 .text 内嵌跳表数据岛）。批 2 全文件扫描的 27551 个失败位点确认几乎全是数据段噪声。整数核覆盖 99.3% 位点，SSE（~900）与系统指令（~90）归批 6/7。

## 维护者评审轮（2026-11-07，soft reset 后进行）

三个真收获，外加一次全量整洁清扫：

1. **tidy 门禁有洞（自批 1 起）**：`.clang-tidy` 的 `WarningsAsErrors` 为空（为编辑器友好），clang-tidy 遇警告照样退出 0，而验收段只看退出码——79 条警告穿越了绿灯，被维护者 VSCode 亮出来才现形。修法：验收的 tidy 段改为数输出里的 `warning:` 行，非零即败。此后「0 警告」是机械事实不是口头约定。
2. **64 位 div/idiv 是坏的（clang-analyzer 立功）**：被除数用 `rdx << 64` 拼接是 UB，实际值也是错的——64 位除法从未被测试覆盖。重写为 `udiv128`（移位减法，显式跟踪 `r << 1` 的进位，除数大于 2^63 也正确）加 `abs128`（idiv 的 128 位取模长），宽度 8 走真 128 位路径，宽度小于 8 保留原逻辑并显式写出合法宽度让分析器可证。五条回归测试钉住（2^63 商、余数、商溢出 #DE、负 128 被除数、正溢出 #DE）。
3. **imm32 进 64 位操作数没有符号扩展**：`mov rax,-4`（C7 形式）得到 0x00000000FFFFFFFC。修在解码器一处：kSz4 在 isz==4 且 opsize==8 时符号扩展，81 组 ALU、69 imul、68 push、A9 test 全部一起修正。差分回归不变（渲染按宽度截断，文本无感）。

整洁清扫：79 条警告清到 0。大头是 readability-trailing-comma（指定初始化列表补尾逗号）和 modernize-use-auto（cast 初始化写 auto），另有 bugprone-signed-bitwise 的两处真语义位移（sar 的算术右移、imul 的符号填充测试）以 NOLINT 加注释保留——`sign_fill_of` 助手把后者收进一处。教训一条：`>> 3` 的裸字面量是有符号 int，该检查连右操作数都管，所以位移字面量一律带 U 后缀（全仓本来如此，新代码两处漏了）。

评审轮顺带：bit.hpp 位域词汇落地（见「内核这批长出来的部分」），bt 内存形式的符号偏移改为「符号扩展进 u64 再无符号移位」的写法（位结果与算术移位恒等，免 NOLINT）。

## 开放项

- fs/gs 段基址、CR/DR、SSE、系统指令、端口 IO：解码白名单在，执行子集不在，命中即 kUnsupported 带 RIP。批 6 挂设备方言时逐批点亮。
- 取指 bulk fetch 与代码页缓存：留待吞吐预算逼门（见基准节）。
- 中断线、事件取消、快照接口：批 4 范围，本批未动（调度器契约未变）。
- 基准两场景偏窄（纯 ALU 循环、访存混合循环），批 6 有真客体后再加引导场景。
