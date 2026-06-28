# 029 — coverage C0-2:QEMU 差分 oracle 硬化(eyeball → 可执行本地门)

> 2026-06-29。coverage Phase 0 第二批。COVERAGE-METHODOLOGY §4。
> 起点:`scripts/qemu_cortex_m3_oracle.sh` 是**只 eyeball** 的脚本——打 actual 三行 + "expect" 三行,人眼比对,退出码恒 0;固定端口 12345(`ctest -j` 并行会撞);无工具链 guard;gdb 名写死。无法 `add_test` 注册成本地门。本批硬化四项,使其成为可执行的正确性护栏。

## 改动(4 项,全在 `scripts/qemu_cortex_m3_oracle.sh` + `test/CMakeLists.txt`)

1. **真退出码**(①):gdb 输出捕获进 `$OUT`,awk 按 label(`$1==L`)抽 4 个 hex word,与 `declare -A EXP` 里的 verified-expected 逐字比对;任一不符 → 打 MISMATCH + `exit 1`;全符 → `exit 0`。缺 label 行 → MISSING(防 gdb 静默失败假绿)。
2. **缺工具链 `exit 77`**(②):起手 `for t in AS LD GDB QEMU; command -v` 全检,缺任一 → skip 信息 + `exit 77`(ctest 识别为 skip 不 fail)。
3. **免端口撞**(③):`$QEMU_GDB_PORT`(默认 12345)可配 **+** CMake 层 `set_tests_properties(oracle_cortex_m3 PROPERTIES RUN_SERIAL TRUE)`——RUN_SERIAL 保证 oracle 独占运行,`ctest -j` 下端口恒空。动态端口易竞态,RUN_SERIAL 更稳。
4. **gdb 可配**(④):`$ARM_GDB`(默认 arm-none-eabi-gdb)+ `$ARM_AS`/`$ARM_LD`/`$QEMU` 全 env 可配。

注册:`test/CMakeLists.txt` `find_program(QEMU_SYSTEM_ARM)` → 在则 `add_test(NAME oracle_cortex_m3)`,不在则 STATUS 跳过(仿 HAL UART 条件注册)。

## 决策

- **RUN_SERIAL 而非动态随机端口**:随机端口仍可能在极端并发撞 + 引入 retry 复杂度;RUN_SERIAL 在 CMake 层一锤定音——oracle 是唯一占 gdbstub 端口的测试,独占运行最简最稳。端口仍可配(`$QEMU_GDB_PORT`)作 belt-and-suspenders。
- **配置门控用 `find_program`(configure-time)而非运行时 `exit 77`**:find_program 决定**是否注册**测试,跟 HAL UART `if(TARGET hal_uart_firmware)` 同模式——宿主无 qemu 则该测试**根本不在** ctest 列表,比"注册了但 skip"更干净(且不污染测试数)。脚本内的 `exit 77` 保留作**独立调用**时的 skip 语义(配置门控的二重保险)。
- **expected 写死为 contract**:8 探针的期望值是 notes 017/018 QEMU 实测、并已纠正过 micro-forge 初版(SDIV/0=0)。它们是 micro-forge 单测对齐的**参考契约**,不是随便的魔法数。QEMU 对这些基础 op 不会变;变了就该被本门抓住、人介入。

## 陷阱

- **地板门 baseline 不能因 oracle +1**:`check_test_count.sh` 若把 BASELINE 升到 322,在**无 qemu 宿主**上 oracle 不注册 → 守卫看到 321 < 322 **误红**(狼来了)。修法:地板门数**产品测试**(321,gtest_discovered、跨宿主稳定),awk 排除所有 meta-guard(`test_count_floor|oracle_cortex_m3`,未来 gcov 门追加)。BASELINE 恒 321,host-stable。详见 028。

## 验证

- 独立跑:`ok DIV/ALU/xPSR` 三行 + `PASS`,exit 0;`QEMU=/nope` → skip exit 77。
- `cmake --build build/-j$(nproc)` + `build-rel` 双构建 → **ctest 323/323 全绿**(321 产品 + test_count_floor + oracle_cortex_m3),oracle 双构建均 Passed(~0.85s)。
