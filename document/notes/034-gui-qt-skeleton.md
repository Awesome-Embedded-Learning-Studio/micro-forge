# 034 — Qt6 GUI 骨架 + DIRECTIVES §E(G5a)

> 日期: 2026-06-30
> 阶段: v0.7.0 milestone 04([GUI dashboard](../milestones/04-gui-debug-dashboard.md))
> 状态: ✅ Qt 集成 + offscreen 启动;核心 352/352 不破
> 前置: G1–G3(notes 031–033)

## 背景

GUI 技术栈已定为 Qt6 Widgets([memory `gui_qt_decision`](../../document/ai/)):与 AELS 对齐 + Windows 部署。G5a 把这一定策落地为架构铁律(DIRECTIVES §E)+ 最小集成骨架,先啃最难的「Qt + micro_forge 链接 + WSLg 开窗」这根骨头,再往上砌面板。

## 做了什么(G5a)

1. **DIRECTIVES §E(GUI 子系统,新章)**:把 Qt6 依赖放松写成有意例外——仅 `micro-forge-gui` target 带 Qt6,核心 `micro_forge` 库与 CLI 仍零依赖;单线程确定性(`QTimer` 触发 `SoC::run`,禁 `QThread`/`QtConcurrent` 跑 sim);单一事实来源(只消费 `read_introspection`)。
2. **CMake opt-in GUI target**:`MICRO_FORGE_GUI` option(默认 OFF);`find_package(Qt6 COMPONENTS Widgets)` + `CMAKE_AUTOMOC ON` + `micro-forge-gui` 链 `micro_forge` + `Qt6::Widgets`。**不进核心库、不进默认 build、不阻塞 ctest**。独立 build dir:`-B build-gui -DMICRO_FORGE_GUI=ON`。
3. **`src/gui/main.cpp`**:最小 `QMainWindow` 骨架,证明 Qt6 + micro_forge 链接、WSLg 开窗。CPU/GPIO 面板留 G5b/G5c。

## 验证

- `cmake -B build-gui -DMICRO_FORGE_GUI=ON`:find Qt6 **6.11.1** OK(pkg-config + Qt6WidgetsConfig.cmake 齐全)。
- `cmake --build build-gui`:`micro-forge-gui` 编译链接成功。
- `QT_QPA_PLATFORM=offscreen timeout 3 ./build-gui/micro-forge-gui` → `exit=124`(=timeout kill=**启动并运行 3s 没 crash**)。
- **核心不破**:GUI OFF,`ctest --test-dir build` 仍 **352/352**(CMakeLists 只加 option + OFF if 块,核心 targets 未变)。

## 设计要点

- **opt-in 而非默认**:Qt6 是数十 MB 重依赖,绝不让 `cmake -B build`(核心/CI)拉 Qt。独立 `build-gui` dir 隔离。
- **GUI 是 consumer 不是 core**:`micro-forge-gui` 链接 `micro_forge` 静态库(已存在,G1 起 introspection 也在库里),不把 GUI 代码进 `MICRO_FORGE_SOURCES`。GUI 腐烂/构建失败永远不拖垮核心。
- **offscreen 验证**:`QT_QPA_PLATFORM=offscreen` 让 GUI 在无显示环境跑(WSLg 不可用时也能 CI)。视觉验收(blink 翻转)留 G5c 给用户看。

## 下一步

- **G5b**:CPU 面板 + run/pause——`QTimer` 驱动 `SoC::run(chunk)` + `read_introspection` 填寄存器表;命令行参数加载 firmware。
- **G5c**:GPIO 面板 + blink.elf 验收(视觉看 PA ODR bit 翻转)。
