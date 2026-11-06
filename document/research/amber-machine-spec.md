# 琥珀机 · 网页 x86_64 机器规格 v1.0

日期：2026-11-06。状态：micro-forge 仓内正本，第 9 节开放问题留维护者逐条拍板。

版本沿革：v0 草案经 2026-11-05 一轮独立评审（致命项为零，十二条薄弱点逐条修入正文或列开放问题；底账核对过两处关键事实：Cinux 侧全部引用与源码一致，Linux 32 位入口寄存器合同与 docs.kernel.org 逐字一致）形成 v0.1 终稿建议；同日拍板两事，引擎语言由 Rust 改为 C++23 加 Emscripten（第 1、5.1、5.4 节与开放问题六同步更新），琥珀机并入 micro-forge 完全重写、作为其 x86_64 前端与第二机型、机器内核共用一份；2026-11-06 迁入本仓 document/research/ 并按仓内文档要求重写：剥除评审过程标签与本地临时路径，人时表去掉日历时间换算，技术内容不变。

## 1. 定位与双客体口径

琥珀机是跑在浏览器里的 x86_64 机器：单 vCPU、无固件、无实模式，C++23 经 Emscripten 编成 wasm 单文件产物，全 MIT 自写，只读公开规范学 QEMU 的设备方言，不读不抄 GPLv2 实现。归置：本机是 micro-forge 的 x86_64 前端与第二机型（Cortex-M3 加 STM32F103 是第一机型），虚拟时钟、事件队列、快照、总线这些机器内核共用一份，MCU 教学线与 x86 线两条里程碑互不阻塞。

双客体：Cinux（kernel.bin，自家 BootInfo 交接）与 Linux（tinyconfig bzImage，官方 32 位引导协议）。维持的判断要重新说一遍，因为它决定了整个排期：Cinux 是照着 Linux 的机器模型画的（QEMU pc 机的子集），所以「Cinux 适配了，Linux 也差不多了」在翻译核心层成立。分三层确认：

- 设备方言层：成立。Cinux 硬依赖是 QEMU pc 机的子集，能跑 Cinux 的机器模型覆盖 Linux 的 microvm 式路线。
- 指令层：成立。两客体指令画像同构，99% 位点 Cinux 59 族对 Linux 55 族，进核同为 syscall/sysretq，同一套四级页表、特权级模型、错误码语义。
- 引导协议层：不成立。Cinux 用自家 BootInfo，不吃 boot_params 也不吃 multiboot，需要两份垫片，但每份都是百行级。

总判决：跑真 Linux 的边际成本是几百行固件桩，不是重写。排期口径补一条：「几百行」是代码量口径，真正的成本在 CPUID、MSR、ACPI 的语义对表调试，人时粗估（第 7 节）按此口径把对表调试算成主要项。

## 2. 机器规格表（v0 装配清单）

设备方言以 QEMU pc 机（i440fx + SeaBIOS，Cinux 的 `cmake/qemu/devices.cmake` 是它的子集）与 QEMU microvm（Linux 客体的无 PCI 路线）为参照。e1000、xHCI、NVMe、HPET、fw_cfg、virtio-mmio 已从 v0 表整体移除，进第 10 节后档停车场，防规格表本身诱导范围膨胀。

| 设备 | 寄存器方言与关键语义 | Cinux | Linux | 档位 | 参照 |
|---|---|---|---|---|---|
| CPU 核心 | x86-64-v1 + SSE2 子集 + RDRAND + invariant TSC + TSC-Deadline，单核 | 硬（内核 SSE2 向量化占 6.7%） | v1 基线够（2024-12 Linus 否决提基线），RDRAND 供熵 | v0 | 指令审计、Firecracker guest_configs |
| CPUID | 定稿：vendor GenuineIntel、family 6 老 model（顺带躲开一串缓解措施 MSR）。叶 0/1/2/4、0x80000001-08、0x15 报 TSC 精确比率（免校准）、80000007H EDX[8] invariant TSC。报 FPU/SSE/SSE2/RDRAND/TSC-Deadline。不报 XSAVE（逼内核留 FXSAVE 路径）、PCID、AVX、x2APIC、LA57（80000001H EDX[26]=0）、叶 7、hypervisor 叶。特性位与实现严格一对一 | 需要基础叶 | 探测更多，叶面已编圆 | v0 | arch/x86 tsc.c 校准优先级 |
| Local APIC | xAPIC MMIO 0xFEE00000，MSR 0x1B 值必须等于 MADT 给的地址，Cinux 拒收 x2APIC。spurious/EOI/timer 全实现。timer 两种模式都做：递减计数器留给 Cinux（SMP 步校准为 0 即 halt），TSC-Deadline（IA32_TSC_DEADLINE MSR）供 Linux，时钟事件直接比 icount，免频率校准环 | 硬 | 硬（CONFIG_X86_LOCAL_APIC） | v0 | Cinux local_apic.cpp、smp.cpp:168-187 |
| IO-APIC | 0xFEC00000（址由 MADT 给），redirection 表，GSI 0（IRQ0 心跳）/1（键盘）/12（指针） | 硬 | 硬（microvm 式 ACPI 路线） | v0 | Cinux io_apic.cpp、kraxel microvm ACPI 博客 |
| 8259 PIC | 0x20/0xA0，重映射 0x20-0x2F 后全掩码，端口写必须成功 | 硬（过渡兼容，只写不依赖） | 不需要（APIC 模式下） | v0（便宜） | Cinux pic.cpp |
| PIT 8254 | 0x40-0x43 + 0x61。ch0 方波 100Hz 心跳（kTickHz=100），ch2+speaker 门做微秒延时（SIPI 节奏、AHCI 超时用） | 硬 | 可选（时钟事件走 LAPIC） | v0 | Cinux pit.cpp、microvm 文档 |
| RTC CMOS | 0x70/0x71（MC146818），墙钟供 ext2 时间戳，0x80 口写当延时槽。墙钟值由配置种子确定性派生 | 硬（读垃圾也不 halt，但端口被摸） | 不需要 | v0（便宜） | Cinux rtc.cpp |
| COM1 16550 | 0x3F8，标准 16550（LCR/DLAB/FCR/LSR），115200 8N1。Cinux 纯轮询，实现 IRQ4 让 Linux 中断驱动也可用 | 硬 | 硬（console=ttyS0） | v0 | Cinux serial.cpp、Firecracker 配置 |
| 8042 PS/2 键盘 | 0x60/0x64。不是「0xAA 回 0x55」一个应答，是完整控制器命令状态机：禁两口、读写配置字节、翻译位置位、自检、再使能，状态寄存器轮询语义全实现（keyboard.cpp:32-63 实测序列）。init 失败即 halt，纯串口档也躲不开。Linux 桌面档复用它走 atkbd，零新增设备 | 硬 | 串口档不需要，桌面档复用 | v0 | Cinux keyboard.cpp |
| ACPI 表 | 程序化生成（QEMU acpi-build.c 思路）。RSDP 双落点：EBDA（端口 0x40E 得给段址）加 0xE0000-0xFFFFF 窗口，两处都要能扫到。Cinux 三表 RSDP/RSDT/MADT（至少列 1 个 BSP，LAPIC 条 enabled 位置位）。Linux 加 FADT（268B 实测体量，SMI_CMD=0 表示已在 ACPI 模式），MADT 加 IOAPIC 条与 IRQ0 到 GSI2 的 INT_SRC_OVERRIDE（QEMU pc 惯例）。DSDT 从 v0 删除：串口与 screen_info 路线都不需要它，ACPI 设备枚举后档再议 | 硬（三表） | 硬（四表） | v0 | Cinux acpi.cpp、kraxel 2020-10 博客 |
| PCI 机制 1 | 0xCF8/0xCFC 枚举，至少回一个 01:06 AHCI 功能且 interrupt_line 合法 | 硬（找盘的唯一路径） | 不需要（无 PCI 路线） | v0 | Cinux pci.cpp、microvm（反面参照） |
| AHCI + ext2 盘 | AHCI 1.3 单端口、slot 0、FIS、IDENTIFY。无盘即 panic（ext2 挂载失败是 safety Check）。ext2 镜像默认 musl 16M 档 | 硬（rootfs 唯一路径，无 initramfs 备选） | 不需要（initramfs 当 root） | v0 | Cinux ahci.cpp、file_world.cpp:63-75 |
| VESA 线性帧缓冲 | 1024x768x32，普通内存当 LFB，JS 侧 blit 到 canvas。垫片路径下 VBE 查询被短路，几何直接写进交接结构 | 桌面（FramebufferInfo 直给） | 桌面（screen_info 同一块内存） | 桌面档 | Cinux framebuffer.cpp、LWN simplefb |
| evdev 输入 | /dev/event0 键盘、event1 绝对坐标指针。琥珀机把浏览器键鼠事件翻译成 PS/2 扫描码与指针流 | 桌面 | 桌面（复用 8042 + atkbd） | 桌面档 | Cinux input_node.cpp:80-81 |
| isa-debug-exit | 0xf4 写值即退出模拟，CI 验收用 | 不感知 | 不感知 | v0 | devices.cmake 测试桩 |

中断、时钟、总线立场：

- 中断：LAPIC + IO-APIC 是唯一真实后端，两客体在此汇合。8259 保留为被掩码的遗留桩，因为 Cinux 引导期要写它的端口。MSI/MSI-X v0 不做。
- 时钟：TSC 是时钟源（叶 0x15 报精确比率免校准，invariant TSC 位置位）。时钟事件：Linux 走 LAPIC TSC-Deadline（直接比 icount），Cinux 走 LAPIC 递减计数器，PIT 因 Cinux 硬依赖而存在，天然兜底校准路径。HPET 不做。全部计时按虚拟时间分频，绝不接真实墙钟。
- 总线：无 PCIe。PCI 机制 1 仅为 Cinux 的 AHCI 寻径保留。未映射端口的默认语义照 QEMU：读回 0xFF，写忽略，打日志。
- 装配：设备按规格表的客体列与档位装配，机器配置里逐件开关，两客体各自的验收档互不阻塞。

## 3. 引导交接协议

总则：琥珀机自己当 bootloader，v0 不进 16 位世界。实模式、BIOS int 服务、UEFI 全部不实现，两个客体各一条薄垫片，共享同一个 E820 生成器。MBR/stage2/pm/lm 四段在垫片路径下成为不被执行的死代码，保留在 Cinux 仓库里，将来做 SeaBIOS 方言固件档（实现 INT 13h AH=42h、INT 15h AX=2401/E820、INT 10h VBE 三样即可复活它们）留给教学后档。

统一交接模型：机器配置描述一份初始状态（客体镜像、入口模式、初始寄存器组、内存映像、设备开关），模拟器据此构造状态后开跑。同一配置加同一输入事件流等于同一 icount 序列，这是确定性与快照验收的地基。两种入口模式共享 E820 生成器（0-0x9FFFF 可用、0x100000-内存顶可用、ACPI 表区标 reserved）与客体内存布局。

### 3.1 Cinux 侧：长模式直接交接

接口头文件只有两个：`kernel/boot/image_header.hpp` 与 `kernel/boot/boot_info.hpp`，垫片照它们生成：

1. 解析 kernel.bin 头（ImageHeader，magic 0x00114514、version 1），按 load_paddr（0x200000）放置镜像，entry 从头部取。校验照抄 ValidateImage 规则：落在单一 E820 可用区、不超 0x40000000、不与 boot 占用区重叠。
2. 构造 BootInfo（magic 0x00114514、version 1、struct_size、e820_count、最多 128 项 E820、FramebufferInfo、内核四装载事实），放低内存空闲区。
3. 建初始页表：低 1GB 恒等映射加高半区映射（VMA 0xFFFFFFFF80200000 映到 LMA 0x200000），CR3 指向它，CR0.PG=1，EFER.LME=1。GDT 给任意合法的 64 位代码/数据段即可，entry.S 自己装内核的 GDT/TSS/IDT。
4. 寄存器交接，照 LmEntry 的现场（lm.cpp:7-31 实测）：RSP=0x90000（kPmStackTop 先例）、RDI=BootInfo 的高半区地址、数据段寄存器装好、关中断。入口跳转：RIP 照 LmEntry 实际行为复刻为 kHighHalfBase + entry，不跳裸物理地址。entry.S 全程 RIP 相对，低地址进大概率也能跑，但那是未测路径，照实测路径复刻最稳。0x4F00/0x4F08 邮箱可顺手写上兼容，寄存器直接给值即可。
5. Cinux 仓库改动点：原则上零改动。需要的只是 kernel.bin 产物（已有）加一份 amber 机器描述文件。CI 加同步检查：boot_info.hpp 或 image_header.hpp 一变（magic/version bump），垫片生成器重跑并 diff。
6. 硬依赖兜底：BootInfo 的 FramebufferInfo 即使串口档也填合法值（内核容忍死帧缓冲，但填了不亏）。

### 3.2 Linux 侧：官方 32 位引导协议

走 docs.kernel.org arch/x86/boot.html 的 32 位入口，实模式 setup 代码不执行：

1. 解析 bzImage：setup_sects 算保护模式内核偏移，保护模式内核放 0x100000，boot_params 放 0x10000，cmdline 放 0x20000，initrd 放 0x6000000（96MB，低于 4G，32 位字段够用）。
2. 填 zero page 必填项：type_of_loader=0xFF、loadflags 含 CAN_USE_HEAP、heap_end_ptr、cmd_line_ptr（协议要求即使不支持命令行也要填，可指空串）、ramdisk_image/ramdisk_size、vid_mode、E820 表。桌面档加 screen_info 的 lfb_base/lfb_width/lfb_height/lfb_linelength/bpp。
3. 入口状态：32 位保护模式、分页关、GDT 含 __BOOT_CS(0x10)/__BOOT_DS(0x18) 4G 平坦段、关中断、ESI=boot_params 基址、EBP/EDI/EBX=0，RIP=startup_32。内核 compressed/head_64.S 自己开分页进长模式，之后全程 64 位。64 位入口（要预建恒等映射）不值得走。
4. cmdline 定稿：`console=ttyS0 earlycon=uart8250,io,0x3f8 nokaslr random.trust_cpu=on init=/bin/sh`。random.trust_cpu=on 让 RDRAND 计入熵信用，否则 crng 快速就绪的验收有落空风险。
5. 内核选型：Buildroot tinyconfig 级 bzImage 约 1.5MB，initramfs 直接当 root，一个块设备都不用实现。内核解压器与内核本体都按 -mno-sse 编，指令面不涨。
6. 确定性注入源定义（档二验收第 2 条与第 4 条同时成立的先决条件）：RDRAND、RTC 墙钟、初始 TSC 全部由配置种子确定性派生，同一种子重跑产生同一序列。RNG 派生状态、事件队列、RTC 状态列入快照 blob 的设备段。否则 RDRAND 的随机值混进熵池，SLAB freelist 随机化与 canary 一变，链表遍历长度就变，逐位一致的 icount 不可能成立。

### 3.3 快照锚点

引导完成态（Cinux busybox shell 就绪、Linux init 就绪）是天然快照锚点，交接验收通过后立即取基线快照。wizer 式预启动（container2wasm 先例）以此为原料，把引导完成态烘进产物做秒开，已从后档提为档二的体验项，见第 7 节。

## 4. 双客体硬件需求差量表

| 维度 | Cinux 到串口 shell | Linux 到串口 shell | 谁多付 | 说明 |
|---|---|---|---|---|
| 引导交接 | BootInfo（0x00114514）长模式直跳 | boot_params + startup_32 32 位协议 | 各付一份薄垫片 | E820 生成器共享 |
| 实模式 + BIOS | 垫片路径不需要 | 不需要 | 零 | MBR/stage2 死代码化，16 位指令集整个不实现 |
| ACPI 表 | RSDP/RSDT/MADT 三表 | 加 FADT（约 268B）与 MADT 的 INT_SRC_OVERRIDE | Linux 略多 | DSDT 已从 v0 删除，ACPI 设备枚举后档再议 |
| 块设备 | PCI + AHCI + ext2 盘，硬依赖，无盘即 panic | 零（initramfs 当 root） | Cinux 多付一整个存储栈 | Cinux 侧 v0 单件最贵的设备，零改动原则的代价，如实记账，维持 |
| 输入 | 8042 自检状态机硬依赖 | 串口档无，桌面档复用 8042 + atkbd | Cinux 多付，桌面档零差 | Linux 键盘零新增 |
| 时钟 | PIT 硬依赖（100Hz 心跳 + ch2 延时），LAPIC 递减计数器 | LAPIC TSC-Deadline + TSC，免校准环 | 名义上 Cinux 多付，实际同一 LAPIC 两模式都做 | HPET 两边都不做 |
| 熵源 | 不在乎 | RDRAND（种子派生）+ CPUID 一位 + random.trust_cpu=on | Linux 多付一条指令一个位 | 免 crng 卡秒级 |
| CPUID 叶面 | 基础叶够 | TLB/cache/brand 叶要补 | Linux 多付几十行 | 特性位必须自洽，已定稿到字段级（第 2 节） |
| SIMD | 内核 6.7% SSE2 硬需求 | 内核 -mno-sse，用户态 ifunc 按 CPUID 走 SSE2 | 同一套子集覆盖双方 | 解释器按 Cinux 口径实现（取更高者），FXSAVE 真存真恢复 |
| 网络 | 桌面档整体后档 | 桌面档整体后档 | 零 | 串口档都不需要 |
| 显示 | 桌面档 FramebufferInfo 直给 | 桌面档 screen_info + FB_SIMPLE/fbcon（优先），simpledrm 备选 | 零差 | 同一块 LFB 内存，一套 blit 代码两用。FB_SIMPLE 比 simpledrm 小一个量级，tinyconfig 不开 DRM |
| 总计（给维护者） | Linux 增量 = boot_params 填表器 + FADT/override + CPUID 补叶 + MSR 白名单 + RDRAND 派生 + initramfs 加载器，代码量合计几百行。它复用 Cinux 已付的一切：LAPIC/IOAPIC、串口、TSC、ACPI 核心、页表遍历、syscall 路径、整套指令集。排期口径：几百行是代码量，CPUID/MSR/ACPI 语义对表调试是人时主要项，见第 7 节 | | | |

## 5. 解释器与软件 MMU 方案

### 5.1 语言与交付形态

C++23（2026-11-05 拍板，取代原 Rust 选型），`-fno-exceptions -fno-rtti`，与 Cinux、micro-forge 同一门写法。工具链 Emscripten（组织内已有 C 系引擎编译进 wasm 的验证先例），编 wasm32，产物单个 .wasm 文件，extern "C" 导出函数加共享线性内存定义接口，不用 embind，胶水手写 C ABI。C 系引擎进 wasm 的先例是 container2wasm 的 Bochs 路线。引导垫片直接 include Cinux 的 kernel/boot/image_header.hpp 与 boot_info.hpp（纯结构定义头），编译期单一事实源，原开放问题六随之销掉。许可纪律：只读公开规范（boot protocol、fw_cfg spec、kraxel 博客）学方言，不读不抄 QEMU 的 GPLv2 实现。

### 5.2 执行引擎

v0 是带基本块预译缓存的线程化解释器，不做运行期 JIT。本节原定「纯解释器」，依 micro-forge 第一版的实测教训修订：朴素逐条解释效率低到几乎不可用，时钟不精确直接毁掉交互体验，结论是解码开销必须摊销、时钟纪律必须成文。结构：以基本块为单位预译成微操作数组（每项＝处理器函数加预取好的操作数），以客体 PC 为键缓存；对代码页的写触发按页失效重译，Linux 解压器与 alternatives 的自改写因此依然正确，且这套按页失效正是将来上运行期 JIT 的前置件，做了不白做。解码器本体学 QEMU decode-new.c.inc 的表驱动：单字节、0x0F、0x0F38、0x0F3A 四张 256 项表加统一 modrm 通道。icount 语义保留：微操作逐条计数，指令边界依然精确，确定性与快照验收不受影响。

指令面 v0 约 150 族 mnemonic、估 500-700 个 opcode+modrm 组合：

- 整数/分支/传送约 60 族（静态 99% 位点需 59，运行时 99% 执行量需 54）
- SSE2 子集 25-30 族：movups/movaps/movdqa/movdqu/movq/movd、punpck* 系列、packuswb、pshufd、pxor/pand/por、psrlw/psllw、paddd
- 原子 6 族：lock cmpxchg、lock xadd、xchg、bts/btr/btc（带 lock 前缀语义）
- 字符串 4-6 族：rep movsb/stosb、lodsb/scasb
- 系统 25-30 族，清单即 Cinux 实测表：syscall/sysretq/swapgs/iretq/invlpg/cpuid/rdmsr/wrmsr/rdtsc/lgdt/lidt/sgdt/sidt/hlt/cli/sti/pause/in/out 各宽度/mov cr0-cr4/dr 系列
- FXSAVE/FXRSTOR 从 stub 升级为必做：真存真恢复那 512 字节。x86-64 基线必报 FPU 与 SSE2，内核任务切换走 FXSAVE 保存用户态 XMM，stub 不回写会算坏 fork 之后父进程的 SSE2 数据。XSAVE/XSAVEOPT 不报，逼内核留在 FXSAVE 路径
- endbr64（f3 0f 1e fa）按 NOP 前缀解码，新工具链编译的 busybox 里有
- 明确不做：x87（遇到即 panic 报 RIP，要精确再挂 Berkeley SoftFloat）、AVX（CPUID 不报位，客体走不进去）、APX、16 位实模式全家桶

MSR 白名单（模拟器实现的高发出错点，不能「一小撮」带过）：内核引导期要写 EFER、STAR/LSTAR/CSTAR/SFMASK、KERNEL_GS_BASE、FS/GS_BASE、APIC_BASE、IA32_TSC_DEADLINE、PAT、MTRRcap、MCG_CAP、IA32_MISC_ENABLE 等，预计 20-40 个。方法论：用自写 QEMU TCG 插件把 tinyconfig 引导期的 rdmsr/wrmsr 全量录制成白名单进仓库，白名单外的 wrmsr 立即 #GP 并带 RIP 与寄存器现场报告，绝不静默。

### 5.3 软件 MMU

学 QEMU 的单张直接映射表 + addend，不学 v86 的全容量 TLB（那是 32 位客体的做法，48 位地址空间放不下）：

- 形状：按 mmu_idx（CPL 分用户/内核两张，可扩）一张 4096 项直接映射表，index=(页号 and 4095)，tag=虚拟页号，项内打包 addend（wasm 偏移减客体物理）、权限位、MMIO/脏标志（QEMU 做法，折进项标志，快路径上没有范围判断）
- 快路径：载项、比 tag、一次加法得 wasm 指针。miss 慢路径：四级 PML4 走查（4 次 wasm 内存读）加 A/D 位回写，错误码产生 #PF 并填 CR2
- 失效策略：CR3 写全刷、INVLPG 单项失效、PCID 不做。内核恒等映射的大页区段缓存列为后档优化
- 两客体同为四级页表、同特权级模型、同错误码语义，一份 MMU 两客通用

### 5.4 客体内存与 wasm 基线

铁律一条：客体物理地址 0 恒等于 wasm 线性内存偏移 0，直接当偏移用（v86/QEMU 同思路）。低 1MB 恰是两条引导协议的必争之地：Linux 的 boot_params 在客体物理 0x10000、cmdline 在 0x20000，Cinux 的交接栈在 0x90000、BootInfo 也在低内存。照 v0 草案的「0 起几 MB 放模拟器自身数据」，模块静态数据（wasm-ld 默认放线性内存低端）会一开机就覆盖 boot_params。

正确布局：模拟器自身状态（寄存器、TLB、设备状态、C++ 静态数据）绝不落在客体物理空间内。Emscripten 默认把静态数据放线性内存低端，恰好就是本条要防的事，须用链接选项（global-base 一类机制）把它挪到 4GB 顶端区域，或另行独立管理。wasm memory 声明 initial = 客体 RAM（512MB 起步，Cinux 实测 1024M 是上限不是下限）+ LFB 窗口 + 自身数据区，maximum = 65536 页（4GB）写死。「浏览器为 wasm32 保留整段 4GB 保护区、访存零显式边界检查」的成立条件就是这句显式 maximum，不声明的话个别引擎走显式检查路径。memory64 已并入 Wasm 3.0，Chrome 与 Firefox 2025 年初正式支持，但实测慢 10% 到 100% 且 Safari 未稳定，v0 不上，实现期再实测一次留档，想要大于 4GB 客体内存时重议。

### 5.5 确定性、快照与预启动

虚拟时间 = icount × K，每条指令 icount++。设备计时（PIT ch0、LAPIC timer）按虚拟时间分频，最小堆管到期事件，键鼠注入走带虚拟时间戳的事件队列，setTimeout 只用于 UI 刷新和让出主线程，绝不驱动设备行为。交互态加实时调速器：执行领先墙钟就让出主线程等待，落后就如实慢放并在 UI 标注（不静默加速也不静默跳时间），客体与设备永远只看见虚拟时间，墙钟绝不参与语义。micro-forge 第一版的教训是时钟感一旦失真，交互体验崩得比速度本身更快，调速器是必做件不是优化项。

快照为版本化 blob：{magic、version、寄存器段、系统寄存器段、客体内存段、设备段}，设备段含 RNG 派生状态、事件队列、RTC 状态（第 3.2 节第 6 条）。TLB 不序列化，恢复后全刷重建（v86 save_state 先例）。快照工程量分两步：v0 只做内存内 blob 加下载导出，isa-debug-exit 验收链不依赖浏览器持久化。OPFS 持久化（512MB 原始 blob 必须压缩，客体内存大量零页，压到几十 MB 现实，加脏页增量，加 Safari 约 1GB 配额与 ITP 七天不交互清储的提示）列 v1。预启动：引导完成态快照用 wizer 烘进产物做秒开，档二体验项。

### 5.6 部署约束

GitHub Pages 设不了 COOP/COEP 响应头，SharedArrayBuffer 拿不到，跨域隔离没有。单线程解释器选型不受影响，但要写成明确约束：worker 之间只能 postMessage 通信，OffscreenCanvas 可用。这两条写进架构文档，免得后面按跨域隔离的假设返工。

## 6. 指令覆盖审计与收口

静态与运行时双审计，白名单口径定稿：白名单 = 静态并集 + 运行时并集 + 余量。99% 执行量覆盖的是条数不是族数并集，运行时族数会高于静态 55 族的口径，按并集加余量收口，不按单一数字。

1. 静态：capstone + pyelftools 直方图脚本扫 Cinux ELF、busybox 静态 ELF、vmlinux（extract-vmlinux 解出），外加三块 v0 草案漏掉的输入：arch/x86/boot/compressed 解压器是独立二进制，extract-vmlinux 解出的恰好不含它。alternatives 的替换代码住在 .altinstructions/.altinstr_replacement 节，在 .text 之外。vdso64 是嵌在 vmlinux 里的另一个完整 ELF，用户态 gettimeofday 走它。三份直方图并入白名单基线进仓库。
2. 运行时：解释器 dispatch 处按指令族挂 u64 计数，退出时打印，开销个位数百分比，进 CI 当每条 commit 的指令分布回归基线。
3. 交叉核对：QEMU TCG 计数插件（实跑 Cinux 引导 6150 万条）做参照分布，模拟器自己的分布与之对量级。
4. 遇未实现 opcode 立即 panic 带 RIP 与寄存器现场，绝不静默跳过。
5. Linux 特有偏差处理：静态字节不等于运行时字节（alternatives 补丁、static key 翻转），审计以运行时计数为准，必要时 QEMU gdbstub 停机 dump 补丁后 .text 再反汇编。
6. 已知采样空洞如实记账：QEMU 实跑那次 ext2 挂载被拒只覆盖到内核 init，用户态 290 处 syscall 未执行，用模拟器自己的计数器补。

## 7. 里程碑与验收

### 7.1 档一：串口 Cinux shell

内容：长模式垫片交接到 Cinux kernel.bin，init 序列全绿，busybox shell 于 COM1，ext2 根卷挂载。

验收（全部可自动判定）：

1. 串口出现 shell 提示符，ls 列出 ext2 盘文件，echo 命令往返
2. 同一配置连跑两次，总 icount 与逐指令族直方图逐位一致（确定性）
3. 运行中快照、恢复、继续跑，后续 icount 序列与不中断的连续运行完全一致
4. 全程零未实现 opcode panic（指令白名单全覆盖的硬证据）
5. 引导指令数与 QEMU 实跑基准（6150 万条）同数量级、同路径（串口日志与 QEMU 实跑基准日志差分在容忍内）
6. isa-debug-exit（0xf4）写入即退出，CI 无人值守判定通过或失败

### 7.2 档二：Linux tiny shell

内容：32 位协议垫片到 tinyconfig bzImage（约 1.5MB）加 Buildroot initramfs 当 root，/bin/sh 于 ttyS0，一个块设备不实现。

验收：

1. earlycon 出字，内核解压与启动日志完整，无探测死循环
2. dmesg 出现 crng init done 且不拖秒级（RDRAND 派生序列加 random.trust_cpu=on 的直接证据，残余时效风险见开放问题 1）
3. /bin/sh 响应命令，uname -a 输出正确，initramfs 内读一个文件内容正确
4. icount 确定性与快照恢复同档一标准（成立前提：RNG 派生状态入快照，第 3.2 节第 6 条）
5. 运行时指令直方图按第 6 节口径（并集加余量）覆盖，未实现 opcode 零次
6. FADT + MADT（含 INT_SRC_OVERRIDE）下内核自动认到机器
7. 时间预算：6150 万条是 Cinux 的实测，Linux 到 /bin/sh 没有实测量，光内核解压（inflate 1.5MB 压缩包）就可能吃五千万到一亿五千万条，加内核 init 估两亿到六亿条量级。开工前先拿现成 QEMU TCG 计数插件实跑一次 tinyconfig + initramfs 到 /bin/sh，把真实指令数写进本条当基线，按 20-100 MIPS 折算页面内引导时间预算。wizer 预启动为本档体验项：烘焙 init 就绪快照进产物，冷启动到 shell 秒级。裸引导 5-30 秒是常态，属正常预期，但要如实告知

### 7.3 档三：帧缓冲桌面

内容：LFB 1024x768x32 加 evdev（event0 键盘、event1 指针），Cinux vesper fbdev 桌面跑在 canvas 上。Linux 侧同帧验证 FB_SIMPLE + fbcon（优先：tinyconfig 量级；simpledrm 拖整个 DRM core，作备选）。键盘两客体都走 PS/2 路线，Linux 复用 8042 + atkbd 零新增。

验收：

1. canvas 呈现 vesper 桌面，固定输入序列下的截图与金色参照图逐像素容差内一致
2. 键盘输入到 /dev/event0、指针到 event1，桌面点按有可视反馈
3. 输入事件流录制、重放，帧序列逐帧一致（确定性延伸到图形路径）
4. Linux 侧 simple-framebuffer 绑定成功，/dev/fb0 可写（向 fb0 写随机数据 canvas 出雪花即算），fbcon 或最小图形程序出画面
5. 桌面运行态下快照恢复仍成立，Cinux 与 Linux 两客体在同一台机器上都能到桌面档（双客体目标的最终证据）

### 7.4 人时粗估

假设：一名熟练系统程序员全职，有 QEMU/v86 行为参照（只读规范），Cinux 仓库与审计工具现状可用，不含网页 UI 打磨。数字是量级参考，不是承诺。对表调试不可压缩，是档二主要项。

| 档 | 分项 | 人时 |
|---|---|---|
| 档一 | 解码器与指令实现（150 族、500-700 组合、FXSAVE 真实现、基本块预译缓存与按页失效） | 190-270 |
| | 软件 MMU 加确定性、快照框架 | 60-100 |
| | 设备核心（LAPIC 两模式、IOAPIC、PIC 桩、PIT、RTC、COM、8042 状态机、ACPI 生成器） | 130-190 |
| | Cinux 链（AHCI、PCI、ext2 盘、垫片、QEMU 差分验收） | 100-160 |
| | 审计工具与 CI 直方图回归 | 40-60 |
| | 小计 | 480-730 |
| 档二 | boot_params 填表器与 32 位交接 | 30-50 |
| | CPUID/MSR/ACPI 语义对表调试（主要项） | 80-160 |
| | RDRAND/RNG 快照语义加 initramfs 加载 | 30-50 |
| | 时间预算实测加 wizer 预启动 | 30-60 |
| | 小计 | 170-320 |
| 档三 | LFB、canvas blit、金测截图流水线 | 40-70 |
| | evdev 翻译加输入录制重放 | 30-50 |
| | Linux FB_SIMPLE/fbcon 路线 | 20-40 |
| | 快照恢复延伸到图形路径 | 30-50 |
| | 小计 | 120-210（部分可与档二并行） |

总计约 770-1260 人时。工程排序维持：解释器、MMU、设备核心、Cinux 串口链、Linux 串口链、桌面档最后，砍序时桌面档最先。

## 8. 风险与缓解

1. CPUID 自洽性是最大风险。Linux 启动期大量探测，报了没实现的特性会被 alternatives 补丁进未实现指令或直接引导失败。缓解：已定稿字段级清单（第 2 节，老 Intel family 6 顺带躲缓解措施 MSR 串），特性集最小化，运行时计数器兜底抓未实现指令，CI 固定内核版本与配置。
2. MSR 语义对表。白名单外的裸 wrmsr 就是 #GP 崩引导，高发出错点。缓解：TCG 插件全量录制白名单（第 5.2 节），失配即 panic 带 RIP。
3. x87 空档。三个样本静态 x87 位面很小，但 glibc 仍有 57 处。缓解：遇即 panic 带 RIP 快速定位，确有需要再挂 Berkeley SoftFloat（v86 同款），不预付。
4. 解释执行性能。micro-forge 第一版已实测朴素解释近乎不可用，本稿据此把基本块预译缓存定为 v0 必做件而非优化项（第 5.2 节），实时调速器同步入 5.5。Cinux 6150 万条按缓存后 50-200 MIPS 估秒级，Linux 两亿到六亿条估数秒到数十秒。缓解三件：开工前 TCG 实测定时间预算（档二验收第 7 条）、wizer 预启动做秒开、调速器如实呈现慢放。档三桌面若预译缓存仍撑不住实时，运行期 JIT（wasm 内生成，v86 已证可行）从后档升为档三前置项，届时按页失效机制已就位。
5. 浏览器内存上限。iOS Safari 对 wasm 内存实际上限约 1-2GB。缓解：客体内存默认 512MB，早期在 Safari 实测，memory64 留作后手（代价慢 10% 到 100%）。
6. wasm 内存布局。静态数据压住低 1MB 会一开机覆盖 boot_params。缓解：布局铁律与显式 maximum=65536 页已写死（第 5.4 节），CI 加一条开局检查低 1MB 与 QEMU 实跑一致。
7. Cinux 仓库漂移。boot_info.hpp 或 image_header.hpp 改版而垫片没跟上，引导静默失败。缓解：两个头是单一事实源，CI 跑生成器加 diff 检查，magic 与 version 双校验，失配即红灯。
8. 语义陷阱清单（源码审计逐条核实，每条都是 halt 级）：LAPIC 地址必须 MADT 值等于 MSR 0x1B 值、x2APIC 拒收、LAPIC timer 必须递减（Cinux 路径）、8042 是完整命令状态机不止一个自检应答、AHCI 无盘即 panic、PCI 01:06 功能必须在。缓解：每设备一个金测，串口日志与 QEMU 实跑日志做差分回归。
9. TSC 校准偏航。缓解：叶 0x15/0x16 数值成对自洽，Linux 走 TSC-Deadline 免校准，Cinux 的 PIT 本来就实现，校准路径天然有兜底。
10. 范围失控。「能跑发行版」的诱惑与机器越加越多的倾向。缓解：v0 目标写死 tinyconfig + busybox，发行版明确出档，e1000/xHCI/NVMe/fw_cfg/virtio 已移出规格表进停车场，表里每件设备都标了档位。
11. 许可污染。QEMU 是 GPLv2，照抄实现会污染 MIT 目标。缓解：只读规范与文档，看行为不看实现，贡献者须知写明，代码评审盯来源。
12. 双客体互相拖累。为 Cinux 加的设备对 Linux 是死重，反之亦然。缓解：设备按规格表客体列装配，机器配置逐件开关，两客体验收档互不阻塞。

## 9. 开放问题（留维护者）

1. RDRAND 加 random.trust_cpu=on 的实际时效：crng init done 的时机依内核版本而变，档二验收前先在 QEMU 实测同版本内核。若仍拖秒级，备选是 initramfs 里预置种子文件，代价是多一道构建工序。已把派生序列与快照语义写死进正文（第 3.2 节），这里只留时效观测项。
2. wizer 预烘焙快照会增大 wasm 产物体积，与单文件部署的取舍。档二实施时实测增量再定，预期几十 MB 压缩内存换秒开。
3. 白名单「余量」的量化：并集之外留多少。建议 v0 干脆全量实现静态并集加运行时并集（500-700 组合本来就全做），余量问题退化成白名单管理策略，CI 回归只报警不拦截新指令族。
4. 多 vCPU：Cinux 的 AP 启动路径与 Linux SMP 都留了缝，v0 单核写死。何时开看教学需要，开了 SMP 快照与确定性语义要重设计。
5. x87：musl/busybox 路线不需要，glibc 57 处静态位点悬着。遇到真需求再挂 Berkeley SoftFloat，不预付。
6. （已销，2026-11-05 随 C++23 拍板）垫片直接 include Cinux 的引导头文件，编译期单一事实源。CI 同步检查保留（头文件 magic 或 version 变更触发垫片重生成与 diff，见 3.1 节第 5 条），检查住进 micro-forge 的 CI 还是留在 Cinux 侧，随合并落地时按其 CI 布局定。
7. OPFS 持久化与 Safari 清储的产品化提示文案，v1 做的时候定。

## 10. 后档停车场

以下各项 v0 一律不做，只留一句再议，防止规格表诱导范围膨胀：SeaBIOS 方言固件档（INT 13h AH=42h、INT 15h AX=2401/E820、INT 10h VBE，三样即可复活 Cinux 的 MBR/stage2 死代码，教学向）、virtio-mmio blk/net（Linux 块设备与网络的首选，等 v0 之外出现真需求）、e1000、xHCI + usb-tablet、NVMe（前置 HPET）、HPET、fw_cfg、ACPI 设备枚举（DSDT 或等价物）、JIT 加按页失效、memory64 大内存（与 LA57 五级页表绑在一起重议）、SMP、OPFS 持久化与压缩、x87 SoftFloat。
