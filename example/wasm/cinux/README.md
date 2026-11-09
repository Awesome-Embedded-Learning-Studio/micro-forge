# Cinux · wasm 试玩（Vue 3 工程）

在浏览器里引导真实的 Cinux 内核（`kernel.bin` + ext2 根卷），把 COM1 接到页面终端，敲命令看输出。

## 前置

1. **引擎**：仓库根执行

   ```bash
   source tools/use-emsdk.sh
   bash scripts/build-wasm.sh
   cp build-wasm/engine.wasm example/wasm/cinux/public/engine.wasm
   ```

2. **Cinux 镜像**：`~/Cinux/build/kernel/kernel.bin` 与 `~/Cinux/build/boot/ext2.img`（约 192 MiB，页面里用文件选择器加载）。

3. **依赖**：`npm install`（Vue 3 / Vite / TypeScript / vitest / vue-tsc）。

## 运行

```bash
npm run dev        # http://127.0.0.1:8090/
```

选择两个镜像文件 → 点「引导」→ 等内核初始化（状态栏显示指令进度）→ 提示符出现后输入命令，回车发送。

```bash
npm run build      # vue-tsc --noEmit + vite build（dist/）
npm run test       # vitest（引擎 ABI 测试；镜像存在时跑引导测试）
npm run typecheck  # 仅类型检查
```

## 结构

```
example/wasm/cinux/
├── index.html          # Vite 入口
├── package.json        # vue + vite + vitest
├── vite.config.ts      # @vitejs/plugin-vue，端口 8090
├── tsconfig.json       # strict + vue-tsc
├── public/engine.wasm  # scripts/build-wasm.sh 的产物
├── src/
│   ├── main.ts         # createApp(App)
│   ├── App.vue         # 终端页面（文件选择、运行循环、输入）
│   ├── engine.ts       # wasm ABI 的 TypeScript 包装（load/boot/run/console/inject）
│   └── style.css
└── test/engine.test.ts # vitest：ABI 测试 + 引导测试（见下）
```

## 调试记录

- 早期症状「wasm 下内核 halt、串口空白」是误读：内核其实跑到了 shell（串口发送计数 1238 与 native 一致），**读不到**是 `mf_read_console` 把 `dst` 当 guest 物理地址走了 `copy_in`，而调用方传的是 malloc 的 wasm 指针（在 guest window 之外），拷贝必然失败。修成 `memcpy` 到 wasm 指针后 console 正常。
- wasm 的 guest RAM 定为 256 MiB：内核对物理内存清零，256 MiB 约 2 亿条指令，512 MiB 会把引导预算推到 4 亿条以上。

## 与 native 的关系

- `mf-cinux-play`（仓库根 `tools/cinux_play.cpp`）是同一套引擎的 native 版：`build/mf-cinux-play` 直接跑 shell，串口接到终端。
- 本工程只是把同一份 `engine.wasm`（`src/wasm/exports.cpp` 导出的 C ABI）接上 Vue 页面。
