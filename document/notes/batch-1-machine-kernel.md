# 批 1 机器内核 — 笔记

范围与验收见 `document/ai/PLAN.md` 批 1 节。本文记沉淀性的知识：形状决策、门禁经验、开放项。

## 错误通道形状（后续批次直接沿用）

- `BusError{kind, address}`：出总线的错误一律带**绝对 guest 地址**。Region 内部用 offset 报错，Bus 在 `rebase()` 里加回 mapping base 再上传。此后所有外设不用关心自己挂在哪。
- Region 六个位宽操作全部纯虚（维护者拍板，批 1 末改造）：默认"从 u8 字节组合"对寄存器型外设是静默错误，没写就编译不过。RamRegion 的宽访问用文件内 `get_le16/32`、`put_le16/32` 小端 helper（形状抄 Linux `include/linux/unaligned/le_byteshift.h`，一处一位宽、直组装不递归）；移位先入 unsigned 域——/tmp UBSan 探针实测 C++23 下裸 `p[3] << 24` 非未定义（模转换），unsigned 只是让中间值都可表示、不依赖提升细节。第二个消费者出现时 helper 升公共头。
- `map()` 拒绝重叠与回绕（kOverlap / kInvalidMapping），相邻区间合法。错误就地暴露，不留到运行期。
- Bus API 全部 `[[nodiscard]]`：丢返回值=吞地址错误，让编译器拦。

## Scheduler 确定性设计

- 事件 key 是 `(at, seq)`：到期 icount 相同的按提交序号破平，优先队列输出序由此完全确定。
- `dispatch(now)` 是持续弹出的循环：回调里再排的到期事件并入同一轮。这是"回调可重入调度器"语义的基线。
- 事件**取消**未实现。将来外设 reset 需要取消时，思路：给 `schedule` 返回句柄（seq 即天然 id），堆惰性删除 + pop 时校验 seq 存活表。

## 门禁经验（批内踩过）

- clang-tidy 23 的 `modernize-use-ranges` 自动修复**生成编译不过的代码**（换 `ranges::upper_bound` 不换比较器方向）。修法是 projection 写法：`std::ranges::upper_bound(v, value, {}, &T::key)`，已用 /tmp 探针对照经典版逐项实测。CI 接 tidy-diff 后凡 `--fix` 产物必须人工复核。
- `configure_file` 模板（`.hpp.in`）不能过 clang-format：它会在 `@VAR@` 内插空格导致替换失效。format 只扫 .h/.hpp/.cpp，CI 别把 .in 加进去。
- 基线两件批 0 落地时有两处缺口，批 1 补上：HeaderFilterRegex 占位符、CheckOptions 内联短格式（clang-tidy 23 要求显式 key/value）。另补 `LocalConstantCase: lower_case`（k 前缀只管全局与类常量）、禁 `portability-avoid-pragma-once`（本仓只面向 GCC/Clang/Emscripten）。
- Doxygen 门禁第三件：母仓 lint.py 拷贝入 `scripts/doxygen/`，EXCLUDED_DIRS 本地加 `tests`（有注释）。头文件注释规范：@file 横幅 + @brief + @param[in] 方向 + expected 返回值一律 @return 说明错误通道。横幅暂不带 @author/@date（待维护者定）。
- 批 1 的"tidy 0 警告"口径只扫了 src/：批 1 末补扫 tests/ 清出 17 条遗留（literal 后缀大写、enum 底类型、misc-const-correctness、乘法拓宽）全部修零。此后 tidy 口径 = src + tests。misc-const-correctness 对"构造期把 `*this` 交给 Bus 的对象"建议顶层 const 是可编译且正确的（构造期逃逸的是非 const 指针），/tmp 探针实测过，不是误报。

## 开放项

- Region 全纯虚后每个外设要写六个函数（不需要的位宽也得手写报错 stub，StubTimer 现状：4 个）。批 3 外设数量上来时建 RegisterRegionBase 类骨架（u32 真语义、其余报错）收掉这层样板。
- Scheduler 无取消、无周期事件（真 TIM2 需要重装语义）。批 4 前解决。
- 快照/版本三元组的序列化接口未起（批 1 只定了 version 常量）。
