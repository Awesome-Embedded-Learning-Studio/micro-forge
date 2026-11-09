# 批 7a 用户空间执行面 — 笔记

## 结果

Cinux 的 shell 跑通了：`kernel.bin` 引导 → 内核初始化全绿 → ext2 挂载 → `Cinux up` → 注入 `echo hi` → **提示符 `/ #` 出现、输入回显、命令执行输出 `hi`**。

这是批 6 留下的验收偏差（「引导到 shell 提示符」）的关闭。

## 打通过程（按顺序，全部是指令面/设备面缺口）

| # | 症状 | 根因 | 修 |
|---|------|------|----|
| 1 | 用户入口首访页 #PF 后递归/终端 fault | #PF 帧 RIP 用了字节偏移；CR2 寄存器未同步；无内核栈切换 | fault_rip 按取指/数据区分；cr_[1] 同步；TSS.rsp0 优先（Cinux 从 per-cpu gs:0 换栈） |
| 2 | strlen 越界读栈顶 | pmovmskb 源操作数槽写成 kXReg（reg 字段），实际在 rm 字段 | kXReg → kXRm |
| 3 | unsupported detail=132 | movd 66 0F 7E 目的槽 kXRm32（xmm）实为 GPR | kXRm32 → kRm32 |
| 4 | f2 0f 70 解码失败 | decoder 的 pmod switch 缺 case 2（F2 表空且不查） | 补 case 2 + F2 表 0x70 = pshufd |
| 5 | **指令流整体错位（cr2 与 rip 对不上）** | **F2 pshufd 表项漏 ImmK::k8**——每条少消费 1 字节 imm8，之后所有指令偏移 | 表项补 ImmK::k8, 2 |
| 6 | ld.so fs:0x10 空指针 | wrmsr 写 0xC0000100/101/102 只进 msr_ map，未同步 fs_base_/gs_base_/kernel_gs_base_ 成员 | wrmsr 同步成员 |
| 7 | 无输入通道 | Serial16550 只有 TX | inject_rx/RBR 读消费/LSR.DR；Machine::inject_serial |

第 5 条是关键：一条表项漏了立即数，让整个用户态指令流逐条错位 1 字节，症状表现为「rip 与 cr2 矛盾、随机 fault」，极难直接定位。定位手段：raise_page_fault 里快照 fault 指令的字节与 cur_length_，用探针打出字节流，与 dump 对照发现偏移。

## 证据

- 引导实测：`Cinux up` → `echo hi` → `hi`（提示符 `/ #` 在 console 中出现）。
- 146 项 gtest 全绿；九段验收全绿。
- 提交：`143baef`（pshufd imm8 + serial 输入）、`205e174`（pmovmskb/movd/F2/MSR base）、前序若干。

## 遗留

- 键盘（PS/2 8042）输入通道还没接（shell 目前走 serial）。
- 设备快照钩子空置（批 6 审查应修项）。
- 命令执行链：`ls` 报 `/bin/sh:`（rootfs 的 busybox applet 链接未验证全），`echo` 正常——ext2/execve 链路是通的，applet 层待查。
- 下一步批 7b：Linux tinyconfig（boot_params 垫片、CPUID/MSR 对表、FADT、TSC-Deadline、initramfs）。
