# 批 4 机器内核收尾 — 笔记

范围与验收见 `document/ai/PLAN.md` 批 4 节。本文记沉淀性的知识：周期链的身份语义、快照的恢复协议、以及测试踩过的两个坑。

## Scheduler 取消与周期（关闭批 1 开放项 2）

- 句柄就是 seq（单调、天然 id）。取消是惰性的：cancel 只往墓碑集合里记 seq，堆顶弹到它才物理删除——活事件永不因取消而重排，`next_deadline()`/`live_events()` 扫描时跳墓碑。堆自管（vector 加 push_heap/pop_heap），不用 std::priority_queue 是为了 const 接口能扫墓碑。
- **周期链的身份语义是本批最重要的一笔**：重装实例沿用首次的 seq，不领新号。第一版重装领新 seq，结果 cancel(句柄) 只杀了早已烧完的首实例，链在 dispatch(100) 里又跑了 16 次（测试值 19 = 3+16，数字当场对账对出来的）。链在任一时刻只有一个活实例，所以 seq 识链不识实例，一击断链。副作用是白赚：快照恢复只需原样带回 (at, seq, period)，不需要链表式追踪。
- next_seq 必须随快照走：烧完的一次性事件消耗过 seq，`max(活 seq)+1` ≠ 原 next_seq，恢复时显式读回，否则恢复后新事件的序号流与原时间线分叉。
- period=0 拒绝过载：降级为 1（「永不挂起」优先于「报错」，调度器没有错误通道）。

## 中断线与 CPU 接收点

- `IrqLine` 是电平线：raise/lower/pending，确认（lower）属于接收方不属于线。批 4 故意只做线不做注入——向量压栈、iret 返回是批 6 的机器，现在把通道做真：Cpu 每条退休指令后观察，线高则调挂载的钩子（带当时 icount）。
- 观察点在 retire 内、dispatch 之后，顺序即语义：设备在 icount K 抬线，钩子同一条退休里就看到 K。
- 不确认的电平线每条指令都触发（电平语义），测试把它写成文档。

## 快照（关闭批 1 开放项 3）

- 三元组：magic "MFSN" + 引擎版本（micro_forge 版本打包成 major<<16|minor<<8|patch）+ 格式版本（信封自身版本，现为 1）。读侧三项全验，错任何一项拒绝，不猜测。负载长度入头，防截断。
- 分节顺序即恢复顺序：RAM、CPU、调度器。各组件自带 save/restore，信封只管编码（小端、长度前缀字节串）。
- **恢复协议的关键分工**：调度器能序列化 (at, seq, period) 表，但回调（闭包设备状态）序列化不了。协议是 `restore_begin`（读回 next_seq 与事件数）→ 逐条 `restore_read_event` → 设备侧认领自己的事件并 `restore_event(info, action)` 重挂回调。事件↔设备的归属映射归设备方（本批测试只有一种事件族；真实设备库按自己的 id 派发）。机器级封装（谁的节、什么顺序）归批 5 的 Machine。
- 快照取在指令边界（run() 的返回点），指令中间状态 v0 不表达。Cpu 的快照面：16 通用寄存器、rip、七标志位（打包进一字节，用 bit 词汇的 mask_bit/extract）、icount、console。IrqLine 的电平属设备段，由设备方存；CPU 里的线指针是接线不是状态，恢复后重挂。
- 往返验收的形状：同程序同设备，直线跑一遍记全量 rip 轨迹与设备触发 icount 表；另一台跑 30 条快照恢复进全新对象续跑，两段拼接与直线版逐位相等（规格 3.2 档二第 3 条的内核级预演，批 5 wasm 产物化后同一形状再验一次）。

## 测试踩坑两笔

- 测试机的 RAM 给了 1MiB（0x100000 字节），代码基址恰是 0x100000——第一条取指就出界，四项测试齐挂且症状互相矛盾（有的看似过了部分断言）。查了半天内核无罪，是测试抄错了容量。教训：越界地址恰好等于「region 大小 + 基址」这种整整齐齐的数，先查测试自己的配置。
- 事件抬线点超出程序总指令数（22 条的程序抬线在 23），事件永远不触发、观察为零。教训：定时类测试的触发点必须落在程序生命周期内，边界条件写在注释里。

## 对抗审查轮（2026-11-07，纪律第 8 条首跑）

三路审查（形状/证明链/家底）加对抗复核，55 条立案经复核维持 48 条（驳回 2、降级 5）。复核者用本机 binutils 2.47 实测对拍七案、crafted 语料实跑差分三案，报告与判定表在 `private/review-batch4/`（不进公开仓）。本批修复清单：

- **bt 组映射错一位（阻断）**：0F BA 的 /4 才是 bt，旧表把 /3 当 bt，真 bt 全被执行成会写内存的 bts。修表 + BtFamily 重钉（objdump 实测为锚）+ 执行器回归（bt 不写内存、bts 写、imm8 负偏移按单字节符号扩展）。
- **F2/F3 强制前缀回退裸表（应修）**：F2 0F 10 曾静默解成 16 字节 movups（实为 movsd），违反白名单宪法。修为 p2/p3 表未命中即拒收。
- **FF 组吞 66（应修）**：66 FF /2 是 call ax 不是 call rax，组内 call/jmp/push 改按 push 的宽度规则。
- **8F /1-/7 照收（应修）**：新增 kGrpPop 组，非 /0 拒收（objdump 拒 8F C8）。
- **REX 在 66 前未作废（应修）**：SDM 规定后随前缀作废 REX，48 66 89 C8 应为 mov ax,cx。修 parse_prefixes（循环顶部判前缀字节集合——注意 0x26-0x3E 是离散值不是连续区间，连续区间会把 0x39 圈进去）。
- **shld/shrd 16 位下溢 UB（应修）**：count 掩到 5 位可取 17-31，`bits - eff` 无符号下溢。按 alu_shift 同款守卫 clamp 到 width-1（硬件本就未定义，clamp 选择写进测试断言）。
- **字符串指令无视 fs/gs（应修）**：字符串族不走 read_operand，拒绝逻辑单独补在 execute_string 入口。
- **bt-mem imm8 源宽（应修）**：imm8 形式的位偏移按单字节符号扩展（寄存器形式按操作数宽）。修的过程中还纠正了自己批 4 笔记里的一个错误断言：「符号扩展进 u64 再无符号右移等于算术右移」——负数上零填充与一填充天差地别，负偏移恰好是要处理的场景。
- 词汇与重复件：sign_extend 四份实现收进 kernel/bit::sign_extend（decoder 立即数、tables disp8、render 显示、cpu 委托）；snapshot.hpp 九段手写小端循环换 le_access；bt 族掩码换 mask_bit；移位计数掩码具名为 shift_count_mask；wants_modrm 死代码（且与 decoder 内联版已分叉）删除；step/run/restore 补 [[nodiscard]]；differential.py 死的 drift 门移除。
- 新测试 11 项：bt 族语义三件、shld 定义性、gs rep movsb 拒收、kFetch、kDecodeError（67 前缀）、restore 短读失败路径、F2/F3 拒收、66 FF 宽度、8F 拒收。107 项全绿。

后置（提示级，记档待安静批次）：internal.hpp 三角色分居、cpu.hpp include 扇出收口（批 5 Machine 落地前是窗口）、字符串族成员表四处重复、render 死残件与 gpr_name/set_rip 死面、差分 prefix-fold 盲区、cmovcc 条件假不读源与硬件可 fault 语义差、cancelled_ 残留、诊断上下文压平（BusError::Kind→FaultId）、Reg 下标 `&15` 五处。

## 开放项

- 快照目前是内存内 blob；下载导出、压缩、脏页增量归批 5/8（规格 5.5 的 v1 段）。
- 事件↔设备归属映射：设备数量上来后（批 6）要立注册表协议，现在测试手工认领。
- RegisterRegionBase 样板（批 1 开放项 1）：继续留批 6。
