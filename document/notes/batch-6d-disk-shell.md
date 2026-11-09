# 批 6d AHCI 盘与用户态入口 — 笔记

## 交付（本批最后一个可交付层）

- **AHCI 1.3**（`kernel/ahci.hpp/.cpp`）：控制器寄存器块 + 单端口命令层。校验过的语义：
  - 命令表 32 字节头（CLB 指向），PRDT 在表 +0x80；DBC 字段是「字节数 − 1」（0 编码 4MiB）。
  - 完成时把 **PRDTL（首项长度）写回命令头**（AHCI 1.3 4.2.1）——内核用它校验 512。
  - GHC 的 HR（bit0）自清、AHCI_ENABLE（bit31）必须保留；PxCMD 的 CR/FR（bit15/14）由 ST/FR 镜像，read-modify-write 不能把状态位写回。
  - IDENTIFY 响应：word 0、27-46 模型串、49 LBA、60 LBA28 容量、83 有效+LBA48、100-103 48 位容量——Cinux 的 admission 逐项检查。
  - 完成中断走 IO-APIC 重定向表取 GSI 11 的向量注入。
- **ext2 盘**：`Machine::attach_disk`；192MB glibc rootfs volume（busybox + 工具链 staging）由 Cinux 的 create_ext2_disk.sh 烘出。
- **BootInfo framebuffer**：640×480×32，physical 指向 LFB（ram_size 处）；内核屏幕控制台 80×30 起来了。
- **fw_cfg 端口对**（0x510/0x511）：空实现，`opt/cinux/init-env` 读签名失败即优雅跳过（不 fault）。
- **用户态基础**：sysretq（RIP←RCX、RFLAGS←R11、CPL=3）、CPL 模型（iretq 按 CS RPL 判断）、用户态 syscall（跳 LSTAR、RCX/R11 保存、清 IF/TF、CPL=0）、#PF 异常注入（IDT 14 中断门、五项帧 + 错误码、按来源压用户 CS/SS、注入时切内核态避免递归）。

## 引导实况（本批终局）

`kernel.bin` 完整引导：serial → PMM（64843 页空闲）→ 地址空间 → 屏幕控制台 → heap → LAPIC/IO-APIC → PIC → syscall 内核态 → RNG → 核心保护（CR4 0x620）→ tick 100Hz → RTC → 键盘 8042 → PCI 枚举（找到 AHCI）→ SMP 单核 → 任务调度 → ext2 挂载（AHCI IDENTIFY + 写回读一致）→ 网络 loopback（ping 4/4）→ **launching shell（/sbin/init 动态链接，ld.so 入口 0x100217E0）**。

**停点**：用户态首访页的 #PF 已注入到内核 IDT 14，内核的 resolver 开始工作（可以观察到内核在读/写用户页表的 PTE），但「惰性映射闭环」（TSS RSP0 换栈、iretq 回用户、resolver 完成）尚未连通——这是**用户空间执行面**的活。

## 验收偏差（诚实记录）

PLAN 批 6 验收原文是「kernel.bin 引导到 shell 提示符」。实测达成：内核初始化全绿、根文件系统挂载、shell 程序加载、切入用户态入口。**未达成**：用户态 shell 的交互（提示符出现、键盘输入回显）——依赖：
1. 完整的 #PF 闭环（TSS 栈切换：硬件从 TSS.RSP0 取栈，我们的 ltr 只存选择子，未读 TSS 描述符基址）。
2. 用户态 syscall 分发跑通（execve 等内核侧处理依赖页表/resolver 稳定）。
3. shell 的终端 syscall（read/write/ioctl）与键盘中断投递。

估规：批 7 的「用户空间执行面」段（内核侧全部就绪，缺的是模拟器的 CPL/异常/中断联调细节）。

## 对抗审查轮（2026-11-08，纪律第 8 条）

三路审查加复核：92 条判定，维持 77、降级 14、驳回 1（复核者跑 boot 探针逐字复现关键输出、objdump 对照、ctest 146/146）。报告在 `private/review-batch6/`。修复：

- **阻断一（NX 位混进物理地址）**：MMU 的 FrameMask 用 `~0xFFF` 会把 PTE 的 bit63（NX）带进页框算术——内核页表项 `0x80000000192005` 被当成物理地址 `0x80000000192000`。引导实际死在此（code=0x800000000019B1F8）。改为只取物理位 12..51。
- **阻断二（fetch 提前消费 #PF 标志）**：`fetch()` 把 `exception_injected_` 清掉后 `step()` 的投递分支变死代码，用户入口首条取指缺页直接终止在 kFetch。改为 fetch 只报失败、step 消费标志。
- **阻断三（#PF 帧 RIP 指向下一条）**：`execute()` 开头就把 rip_ 推进到 rip_after，失败时压帧用了下一条。翻译入口收 fault_rip 参数：取指传取指地址、数据访问传指令地址。
- **CR2 寄存器**：fault 时同步写 `cr_[1]`——内核用 `mov rax,cr2` 读，之前只记在成员里读不到（内核 #PF 诊断显示 cr2=0）。
- **#PF 换内核栈**：Cinux 的 TSS.rsp0 从未设置（tss.cpp 零初始化），它的入口栈在 per-cpu 块的 GS_BASE offset 0（syscall.S 的 `movq %gs:0,%rsp`）；注入时按此切栈，GS/TSS 基址都经 MMU 翻译（它们是虚拟地址，物理直读会得到垃圾——一次调试中读到 0x7FFFFF9DAF18 用户区值就是物理直读的后果）。递归护栏：depth≥2 时终端 fault。
- **中断注入**同样按来源压用户 CS/SS 并换栈。
- **INVLPG** 从空操作改为真实单项失效（内核改页表后依赖它）。
- **门禁漏检**：提交前验收跑的是未跟踪的 ahci.cpp——`git ls-files` 看不见，format 17 处与 tidy 28 条漏网。教训：验收必须在 `git add` 之后跑，或把门禁的文件来源从 `git ls-files` 改为 `git ls-files --cached --others`。
- **psrldq/pslldq**（0F 73 /3 /7）补齐——ld.so 首次执行撞到。

## 测试与门禁

- 146 项 gtest 全绿；tidy 零警告（含 Padding 抑制注释）；九段验收全绿。
- 引导实测用 `bootdbg` 探针（kernel.bin + ext2.img，~1.15 亿条指令，墙钟 40s 量级）。

## 开放项（批 7 起点清单）

- 用户态 shell 交互（批 7）。已定位的续接点（调试沉淀）：
  1. 异常深度在「一次注入配两段 iret」下漂移，已改为回用户态清零（本轮修复）。
  2. 取指跨页时帧 RIP 用字节偏移导致 iret 落进指令中间，已改为指令起始（本轮修复）。
  3. 用户态 syscall 入口（syscall.S）用 `%gs:8`/`%gs:0` 段操作数——执行器对 fs/gs
     段访问目前 unsupported，这是 shell 发第一个系统调用前的最后一道门。
  4. demand pager 修页后，用户页表 PDPT[0] 出现被内核重写的情况（CR3 切回内核页表
     时 rip 仍是用户地址），需要与内核的 LoadCr3/切任务路径对齐。
  调试方法：raise_page_fault 失败分支打 stderr + gdb 断 raise 看返回值序列；
  探针 bootdbg15 对比 RAM 页与 ELF 文件字节（已证明 AHCI 读盘数据正确）。
- 设备快照钩子：PIT/LAPIC/PIC/AHCI/Serial 状态未进快照（审查应修项，批 7 前修）。
- 验收门禁文件来源修正（见上）。
- TSS 结构读取（ltr → GDT → TSS base，RSP0 用于中断换栈）。
- AHCI 多端口/NCQ、NVMe（批次外）。
- 键盘扫描码注入与 console 回显（批 7 交互面）。
