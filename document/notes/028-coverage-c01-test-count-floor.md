# 028 — coverage C0-1:测试数地板门(test discovery regression floor)

> 2026-06-29。coverage 战役(三维度最后一维)起手 Phase 0 第一批。COVERAGE-METHODOLOGY §3。
> 价值命题:`gtest_discover_tests` 在构建期发现测试——一个改坏它的重构(改名/移动测试源/断 include)会让整批测试**静默消失**,构建仍绿、覆盖率蒸发,无人察觉。本批立一道**测试数地板门**:跑廉价的 `ctest -N`(只列不执行),发现数跌破基线即红。

## 改动

1. **`scripts/check_test_count.sh`**(新,1.8k):
   - 跑 `ctest --test-dir <build> -N`,awk 统计 `Test #N:` 行,**排除自身**(`test_count_floor` 是 meta-guard,非产品测试)→ 得"真实测试数"。
   - `< BASELINE(321)` → `exit 1`(红);无 build dir → `exit 77`(skip)。
   - `BASELINE=321` 写死在脚本,注释约束"只升不降"。
2. **`test/CMakeLists.txt`**:`add_test(NAME test_count_floor COMMAND bash …/check_test_count.sh ${CMAKE_BINARY_DIR})`。注册后 `ctest -N` = 322(321 产品 + 1 meta-guard),脚本排除自身 → 321。
3. **修 stale**:DIRECTIVES L5 `222→321` + 指向守卫;COVERAGE-METHODOLOGY §3 同步(CLAUDE.md 早是 321,无需改)。

## 决策

- **floor 取 321(产品测试),非 322(含 guard)**:guard 自身是度量工具,不该算进"产品测试数"。脚本 awk `!/test_count_floor/` 排除自身 → 度量对象恒为产品测试。C0-2/C0-3 后续加的 oracle/gcov 测试**是**真测试,届时数它们、bump 基线。
- **不跑全量 ctest 验失败,只 `-N` 数数**:本门只防"测试消失"(`-N` 发现数暴跌),不防"测试失败"(那是 L1 全量 ctest 的职责)。前者廉价(0.07s),后者已是日常门。职责分离,不重复跑 321 测试。
- **基线写死 vs 配置文件**:写死(L6 省 token,不堆仪式)。"只升不降"靠人工 review 约束,加测试时一行 bump。

## 假设 / 边界

- 基线 321 假设**满语料宿主**(含可选 e2e + armcc 固件测试;本机 e2e 4 个在场)。CI 注册按方法论决定#1**暂缓**——CI 若缺固件语料会数 < 321 误红,留待 CI 注册时处理。
- 不防"删 1 个测试"(321→320 被 guard 自身的 +1 掩在 322→321 边界):地板门防的是**整批消失**(重构吞 5~50 个),单测删除通常是有意且 diff 可见。

## 验证

- 独立跑 `bash scripts/check_test_count.sh build` → `ok: discovered 321 tests >= baseline 321`,`exit 0`。
- `cmake --build build -j$(nproc)` 重配 + 构建 → **ctest 322/322 全绿**(含 `test_count_floor` Passed 0.07s)。产品测试 0 回归。
