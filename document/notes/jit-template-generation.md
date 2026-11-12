# 模板改由汇编器生成（2026-11-13）

## 起因

JIT 的机器码模板原本是手写的字节数组，洞偏移靠人算。二十多轮里至少八次
数错——`kLeaSlot32` 的 dst 洞、`kCmovMemSlot` 的 call_offset、`kImul23Slot`
与 `kGrp3MemSlot` 的长度……每次都要回到打印字节对照的调试循环，累计两三
小时全花在数数上。

汇编器本来就会算偏移，让它算。

## 现在的形态

```
src/arch/x86_64/jit_templates.S          手写汇编，61 个模板
        ↓  as + objcopy + 魔数扫描
build/generated/.../jit_templates_generated.hpp    构建期生成，不进仓库
```

`.S` 里每个洞填一个独占魔数，`scripts/gen/gen_jit_templates.py` 汇编、
扫描、记录 `(种类, 偏移, 大小)`，输出字节数组与洞表。模板边界取自
`tpl_<name>` / `tpl_<name>_end` 符号——长度写错这种事不再可能。

生成物由 `JIT_TEMPLATES_OUT` 指定位置，CMake 以 `BUILD_INTERFACE` 加进
include 路径。不加这层，sanitizer 那个独立构建树会在生成阶段报"路径带
源码前缀"。

## 四个坑

**魔数自相包含**。第一版把 8 字节魔数设成 `0x6b41536500000001`，而 32 位
Base32 魔数是 `0x6b415365`——扫描 4 字节时会在 8 字节魔数的后半截命中。
改成两个半段都不等于任何 32 位值，并且先扫 8 字节、标记占用后再扫 4 字节。

**洞位置必须清零**。生成器原本把汇编产物原样输出，洞位置留着魔数。布局
校验器要求补丁位置为零（块编译器 patch 时才写值），于是
`has_valid_layout()` 对所有新模板一律返回 false。这个 bug 最阴的地方是
失败信息：只有一句 false，不说哪个条件。前三轮我一直在怀疑三元表达式、
怀疑洞表退化成指针——都不对。

**旧注释不可信**。迁移时发现 `kCmovMemSlot` 的 `helper64@41` 实际 @43，
另有六个模板压根没有洞注释（一直靠 translate 里写死的 offset 工作），
`kPushRegHelper` 的 `slot32@2` 实际 @4。手写注释、translate 偏移、测试
期望值，同一个数字存了三份。

**"55/55 语义一致"是循环论证**。我用旧注释填占位生成新汇编，再用同一份
注释填占位反汇编对比——两边错得一样就"一致"了。真正有效的验证只有差分
用例，而它按形态覆盖。

## 迁移方法

前三种自动方案都失败（就地正则、按序对应、按 value 表达式推断）。根因是
同名字段在不同模板里对应不同的洞：`lea` 的 `*source` 是基址，`add` 的
`*source` 是源寄存器，`kAddImmHelper` 的 `*base` 实际是目标槽。

最终走通的路径：**两个头同时 include**，旧模板在 `jit_templates`、新模板
在 `jit_gen`，靠 `jg::` 前缀区分。每迁一块都能编译能验证，不必等整个文件
改完——之前几轮把 include 换掉、未迁移的块找不到符号，只能整文件一次成型，
16 块一次改对的概率太低。

三元模板必须拆成两个方向各写一份：`to_xmm ? kMovxLoadSlot : kMovxStoreSlot`
里两个数组会退化，取到的洞表不是预期的那个。

## 顺带清掉的手写拷贝

- 两个手写模板头（721 + 279 行）整个删除
- `load_code<N>` 靠改模板最后 8 字节切宽度 → 汇编源里四个独立模板
- `cmp_memory_code<Base, Off>` 拼接派生 → 汇编源里直接写完整序列
- 五处测试硬编码的字节布局（长度、偏移、逐字节内容）→ 从洞表取值，
  只留语义断言

## 一个教训

`kPushRegHelper` 的模板读 `[rax+disp]` 而非 `[rdi+disp]`，而块 ABI 是
`invoke(regs)` 通过 RDI 传寄存器文件。修了之后我加了一条 `push rax` 的
差分用例——**把模板改回 `[rax]` 之后它照样通过**，因为 rax 在那次调用里
恰好还残留着同一个指针，UB 碰巧成立。

差分测试比的是"两边结果是否一致"，不是"这条指令写得对不对"。能抓住这类
问题的是字节断言：`57 48 8B B7` 就是 `push rdi; mov rsi,[rdi+disp32]`。

## 剩余 → 已做：模板覆盖反查（2026-11-12）

原文说"可以拿覆盖率数据反查"。现在落地为两个工具：

- **翻译期审计** `./build/bench/mf-tpl-coverage`：把全部差分用例表
  （base/width/alu/extra，330 条）逐条喂给 `translate_insn`（只翻译不执行），
  按"发出的模板字节指针"对照生成头里的 `kTemplateRegistry` 计数，
  打印每个模板的命中次数。零命中 = 没有任何差分用例走到这条翻译分支。
- **运行期探针** `MICRO_FORGE_JIT_TPLSTATS=1` + `mf-jit-kernel-probe`：
  `translate_insn` 外包一层 wrapper 记数（env 关闭时零开销），
  probe 退出前 `mf_jit_dump_template_hits()` 打印内核侧每个模板的翻译命中。

首轮结果（老内核 b58e271）：

- 七个模板在翻译层零引用，纯死代码，已从 `jit_templates.S` 删除：
  `add/sub/and/or/xor_slot_imm`、`cmp_mem_call`、`load_rip`（被
  `load_rip_slot` 取代）。61 → 54 个模板，覆盖率不变（99.9625）。
- 测试零命中但内核在用的 8 个是差分用例缺口（kPushRegHelper 那类盲区）：
  `call_reg_helper`、`grp3_slot`、`grp3_mem_slot`、`imul_mem_slot`、
  `lea_idx_slot32`、`pop_reg_helper`、`ports_slot`、`ret_helper`。
  这些模板的翻译分支真实服务内核，却没有一条差分用例或字节断言盯着——
  下一批补用例按这份清单来。
- 内核零命中但测试有的 6 个（`add_mem_slot`、`load_slot_b/w`、
  `load_idx_slot_b/w`、`push_imm_helper`）是老内核没走到的宽度/形态变体，
  属于正常冗余，不是缺口。
