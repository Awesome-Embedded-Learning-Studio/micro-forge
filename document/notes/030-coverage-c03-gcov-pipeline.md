# 030 — coverage C0-3:gcov 覆盖率管线(一条命令出报告)

> 2026-06-29。coverage Phase 0 第三批,收尾。COVERAGE-METHODOLOGY §2。
> 起点:coverage 工具**零**(`.gitignore` 是唯一痕迹)。本批立 gcov 插桩管线 + gcovr 报告,为后续覆盖率/正确性 hunting 铺好度量地基。

## 改动

1. **`CMakeLists.txt`**:`option(MICRO_FORGE_COVERAGE OFF)`。ON 时:
   - `target_compile_options(micro_forge PRIVATE -fprofile-arcs -ftest-coverage)` —— **PRIVATE** 只插桩 `micro_forge` lib 源,**不染 gtest/test 驱动**。
   - `target_link_options(micro_forge INTERFACE --coverage)` —— **INTERFACE** 让链入 micro_forge 的 test exe 链入 libgcov,运行退出时刷 `.gcda`。
   - 默认 OFF,日常构建(build/、build-rel)零影响。
2. **`gcovr.cfg`**(新,项目根):裸 `gcovr` 出文本表(行指标 + 每文件未覆盖行号)+ 三指标 summary(lines/functions/branches);注释里写全流程 + 两个 override(`--txt-metric branch`、`--html`)。

## 决策

- **PRIVATE 编译 + INTERFACE 链接**(不是 PUBLIC):PUBLIC 会让 compile flag 传播到 test_*.cpp(gtest 驱动)也被插桩——污染 + 拖慢。PRIVATE 锁编译插桩在 lib;INTERFACE 只传播**链接** `--coverage`(libgcov),test exe 链入即刷 .gcda,但 test 源不插桩。实测 36 个 `.gcno` 全在 `micro_forge.dir/src/`,gtest 零 `.gcno`,验证范围正确。
- **文本表默认行指标,HTML 显式要**:gcovr 8 的 `--txt-metric` 单值(一次一个指标);行表的"未覆盖行号列表"对 hunting 最可操作(grep 即定位)。HTML 走显式 `gcovr --html`(浏览用)。不把 HTML 塞进默认 cfg——否则 gcovr 只写文件、stdout 文本表消失,丢掉最实用的终端视图。
- **`gcovr.cfg` 而非 wrapper 脚本**:cfg 是 gcovr 原生机制,`source use-python.sh && gcovr` 一条命令成型,零额外脚本维护。

## 陷阱

- **gcovr 8.6 配置 key 陷阱**(踩了两脚):① `--branches` 是**废弃**别名(→ `--txt-metric branch`),对应 config key 是 `txt-branch` 而非 `branches`(写 `branches = ...` → "unknown config option");② 布尔值只认 `yes`/`no`,**不认 `true`/`false`**(`print-summary = true` → 报错)。gcovr 8 与旧版 cfg 语义有出入,改 cfg 必须实跑验证。
- **`FETCHCONTENT_SOURCE_DIR_GOOGLETEST` 复用**:新 build-cov 配置时 FetchContent 会重新 clone gtest(本机慢/卡)。复用 build/ 已拉好的 `build/_deps/googletest-src`(`-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$(pwd)/build/_deps/googletest-src`)秒配离线。

## 基线(2026-06-29,全 323 测试跑过)

| 指标 | 覆盖 |
|---|---|
| lines | **79.5%**(3034/3814) |
| functions | 88.7%(417/470) |
| branches | **60.0%**(1718/2865) |

行覆盖 gap 清晰(后续 hunting 信号):scb.cpp 29%、machine.cpp 53%、nvic.cpp 62%、stm32f1_rcc.cpp 65%、stm32f1_flash.cpp 64%。flat_memory.cpp / mmio_trace.cpp 100%。

## 验证

- `cmake -B build-cov -S . -DMICRO_FORGE_COVERAGE=ON` → "gcov instrumentation ON";`--build` → 36 `.gcno`;`ctest --test-dir build-cov` → 323/323 + 35 `.gcda`。
- **正常 build/ + build-rel 双构建 323/323 全绿**(OFF 中性,零回归)。
- `source scripts/use-python.sh && gcovr` → 文本表 + 三指标 summary(79.5% / 88.7% / 60.0%)。
