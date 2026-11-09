# 批 6a 指令面地基 — 笔记

范围见 PLAN 批 6 拆分。本段三块：系统指令、SSE2 子集、软件 MMU。

## 交付

- 系统指令：mov CR/DR 双向（CR0/2/3/4、DR0-3/6/7 存值）、lgdt/lidt/sgdt/sidt（GDTR/IDTR base+limit 存值）、cpuid 固定叶表、rdmsr/wrmsr 白名单、cli/sti 真语义（IF 进 rflags 组合）、pushfq/popfq（0x9C/0x9D 解码补上，渲染名对齐 objdump 的 pushf/popf）、rdtsc 由 icount 派生（虚拟时钟、确定性）、swapgs、hlt 保持停、invlpg 占位。
- MSR 白名单：EFER/STAR/LSTAR/CSTAR/SFMASK/FS_BASE/GS_BASE/KERNEL_GS_BASE/APIC_BASE/PAT/MTRRCAP/IA32_MISC_ENABLE/MCG_CAP/TSC_DEADLINE/TSC。白名单外读写 → kFault + FaultId::kMsr，code 带索引。GS_BASE 写入镜像进段模型。
- cpuid 只报已实现的特性位：leaf 1 报 FPU/PSE/TSC/MSR/PAE/CX8/APIC/SEP/MTRR/PGE/CMOV/PAT/CLFSH/MMX/FXSR/SSE/SSE2，ECX 全零（不报 SSE3+）；leaf 7 全零（不报 SMEP/SMAP，避免内核走到未实现路径）；扩展叶 0x80000001 报 NX/SYSCALL/LM。特性位与执行器支持面自洽是硬要求——报多了就是诱导内核去撞 unsupported。
- SSE2 子集（sse.cpp）：movaps/movups/movdqa/movdqu、movq/movd 双向、pxor/pand/pandn/por、paddd/paddq/psubd、punpck 八种、packuswb、psrl/psll 六种（imm8 与 xmm 计数）、pcmpeq 三种、pshufd。128 位访存由两次 64 位总线访问合成；对齐不检查（movdqa 非对齐 #GP 等故障模型）。
- 软件 MMU（mmu.hpp/.cpp）：按规格 5.3 单张 4096 项直映射表 + tag 比对 + 四级走查（每级一次物理 8 字节读）、2 MiB / 1 GiB 大页、A/D 位回写、权限检查（P/W/U/NX、CR0.WP 区分子写只读）、#PF 错误码记录到 CR2 + page_fault_error()。失效：CR3 写全刷（调用方 flush_tlb）、INVLPG 单项。PCID 不做。快路径权限每次复查（TLB 缓存映射，不缓存决定）。默认直通模式，`set_paging(true)` 由引导垫片打开。

## 两课

1. **层级索引拿页号当虚拟地址算**：walk 里 `level_index(vpn, 21)`——vpn 已经是 >>12 的页号，再 >>21 错位 12 位，PD 索引全部指向第 0 项，高地址统统被 2 MiB 大页兜住。症状是「越界地址居然能读」，不是当场崩——又是批 5 空指针毒的同族：坐标错位不报错，只是静默错映射。修法：索引一律对虚拟地址做位移，参数命名分清楚 vaddr 和 vpn。

2. **补丁静默失配**：给 store() 加翻译的 python replace 因为 clang-format 改过函数体开头而没命中，测试立刻用「写只读页居然成功」抓出来——但这靠的是专门为它写的测试。教训还是老一条：批量修改后必须验证命中（assert 原句存在），否则静默空替换和代码 bug 难以区分。

## 测试

- test_system.cpp（系统指令与 SSE，12 项）：cli/sti、pushfq/popfq、MSR 往返与越界、cpuid 叶 1、rdtsc 派生、lgdt/sgdt 往返、mov cr4、movaps/movdqa/paddd/pshufd/movq。
- test_mmu.cpp（6 项）：恒等映射取指与数据、未映射取指 fault 与 CR2、只读页写 fault（WP）、无 WP 子写通过、NX 取指拒而数据可读、TLB 失效后重走。
- 测试拆分：公共 fixture 进 tests/cpu_fixture.hpp，test_cpu（33）/test_system（12）/test_mmu（6）。133 项全绿。

## 开放项

- fetch 逐字节翻译：每字节一次 translate_access，快路径命中也在多一次函数调用；等 6c 真跑引导画像后决定是否按页缓存或加 16 字节快取。
- movdqa 对非对齐地址的 #GP、FXSAVE/FXRSTOR（cpuid 已报 FXSR，但指令未实现——Cinux 进用户态前的任务切换会碰；6c/6d 前补）。
- 1 GiB 大页的 reserved 位检查简化为 bits12-29；实际 QEMU 也这么干，留着。
- syscall/sysretq 执行面（Cinux 引导期只写 MSR，退用户态在 6d）：本批维持 unsupported。
