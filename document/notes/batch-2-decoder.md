# 批 2 x86-64 解码器 — 笔记

范围与验收见 `document/ai/PLAN.md` 批 2 节。本文记沉淀性的知识：表驱动形状、差分方法论与战果、画像基线、开放项。

## 表驱动形状（学 QEMU decode-new.c.inc 的架构思想，未读其实现）

- 四张 256 项表：主表、0F 逃逸表（按 mandatory 前缀 none/66/F2/F3 分四张）、0F38 三字节表（v0 只有 66 0F 38 29 pcmpeqq 一项）。表项描述操作数槽（Slot 枚举）与立即数规则（ImmK），组操作码（80/81/83、C0-C3、F6/F7、FE/FF、0F 01/BA/C7/71-73/AE）由 ModRM reg 字段二次分派。
- IR（DecodedInsn）扁平化：前缀（lock/rep/seg/REX 位）+ mnemonic + cc + 最多三个操作数（寄存器/内存/立即数/隐式）。执行器（批 3）不需要回读指令字节。
- 错误通道 `std::expected<DecodedInsn, DecodeError>`，Kind ∈ {kUnsupportedOpcode, kBadPrefix, kTruncated, kBadModrm}，offset 是流内相对偏移。白名单外一律报错，绝不猜测解码——这是规格第 6 节"遇未实现 opcode 立即 panic"在解码层的镜像。
- 立即数宽度规则单独成枚举（kSz4/k8S/k64/...）：B8+ 在 REX.W 下读 imm64、83 的 imm8 符号扩展、push/pop 的 kP 规则（REX.W 优先于 66）。

## objdump 差分（权威源判定，批 2 的核心验收方法）

`scripts/dec/differential.py`：GNU objdump（binutils 2.47）二进制模式 i386:x86-64 Intel 语法为 oracle，与 `tools/dec_dump.cpp` 的线性解码逐条比对指令边界与渲染文本。规范化层只处理显示差异（助记符列对齐空白、movs 显式操作数形式、无效前缀 rex.WX/data16、段前缀、riz 零索引、RIP 负位移全宽显示、shift 隐式 1 十进制等），不动语义。

最终口径（Cinux kernel.bin 全文件 975728 字节线性扫描）：

- matched 211825，mismatch **0**。
- len_divergence 494：文本一致、长度不同，全部是 objdump 把 (bad) 序列合并成一条的行为，非解码分歧。
- bad_ref 2569：objdump 拒解/纯前缀行，我方按白名单解出（多在数据段，属噪声容忍）。

差分磨掉的真实解码错误（单元测试全部没拦住，全部由差分发现）：

1. REX.X=1 时 SIB index 编码 4 是 r12，不是"无索引"（`if (idx != 4)` 漏了 REX.X 分支）。
2. MOV cr, r 的 CR/DR 号在 ModRM **reg** 字段、GPR 在 r/m 字段——与直觉相反，objdump 是唯一可信仲裁。
3. 66 0F D6 是 movq r/m64, xmm（存储方向）；66 REX.W 0F 6E/7E 是 movq 的 64 位变体而非 movd。
4. FF /2 call r/m64 与 FF /0 inc r/m32 默认操作数大小不同（64 vs 32），组内按 reg 分支定 opsize，且要在 opsize 计算后同步覆盖。
5. 无效段寄存器编码（6/7）要拒绝，不能别名到 gs。

工程教训两条：`out.mnemonic = ent.mn` 的拷贝时机在组分派与变体改写**之后**（两处顺序 bug 都因此产生）；`ent.sz` 改写不影响已算完的局部 opsize，同步要三处一起改（局部、out、make_rm 捕获）。

## 裸客体验收

`tests/data/bare-client.s`（GNU as、Intel 语法）汇编成 flat bin 后 100% 解出（47 条，0 失败），CMake 用 as + objcopy 现场构建，ctest 常驻（44 项测试全绿）。这份客体就是批 3 执行器验收客体的种子：ALU、分支、call/ret、内存、push/pop、syscall 退出。

## 画像基线（kernel.bin 全文件扫描，含数据段噪声）

`document/notes/batch-2-kernel-bin-profile.txt`：解出 304211 条，失败位点 27551（kUnsupportedOpcode 26566、kBadPrefix 981——0x67 地址大小前缀、kTruncated 1、kBadModrm 3）。精确 .text 画像（按 ImageHeader 定位代码段）留给批 3 前处理，届时直方图去噪后作为执行优先级依据。

## 开放项

- 0F 38/0F 3A 表近乎全空：白名单只有 pcmpeqq，差分未在 kernel.bin 命中其他三字节指令，Linux 侧（批 7）若撞见再补。
- 中断线、事件取消、快照接口：批 4 范围，本批未动。
- 解码器对 67 前缀一律 kBadPrefix：64 位客体不会用到 16 位寻址，真客体差分（批 6/7）若出现再议。
