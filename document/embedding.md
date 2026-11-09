# 把 micro_forge 当库用

micro_forge 由三层组成：`base/`（通用组件：缓存、机器布局结构）、内核与指令层（`kernel/`、`arch/x86_64/`）、预设（`presets/`，目前是 Cinux 的引导垫片与机器布局）。嵌入时你只需要 `micro_forge/engine.hpp` 和 `micro_forge/base/machine_config.hpp`。

## 快速开始

```cpp
#include <micro_forge/engine.hpp>

#include <vector>

int main() {
    micro_forge::Engine engine(0x10000000);            // 256 MiB guest RAM
    const std::vector<uint8_t> program = {0xB0, 0x41, 0xF4}; // mov al,0x41; hlt
    engine.load_image(0x100000, program);
    engine.machine().boot(0x100000, 0x90000);           // RIP, RSP
    const auto status = engine.run(1000);
    return status.kind == micro_forge::arch::x86_64::ExitStatus::Kind::kRunning ? 1 : 0;
}
```

`Engine` 是薄门面：构造、`load_image`、`boot_cinux`、`run`、`console`、`inject_serial`、`snapshot`/`restore`。需要设备、CPU、总线时用 `engine.machine()` 拿底层 `Machine`。

## 自定义机器布局

布局是数据，不是代码。`MachineConfig` 描述 RAM 大小、端口窗口、设备内存窗口和 AHCI 的 PCI 身份：

```cpp
micro_forge::MachineConfig config;
config.ram_size = 0x1000000;
config.ports = {
    {.base = 0x2F8, .size = 8, .device = micro_forge::MachineDevice::kSerial},
    {.base = 0x70,  .size = 2, .device = micro_forge::MachineDevice::kRtc},
};
config.ahci_vendor = 0; // 没有磁盘控制器
micro_forge::Engine engine(config);
```

`size == 0` 表示该设备不存在；端口重叠会让构造直接中止（配置错误不静默）。

## 引导 Cinux

```cpp
const auto kernel = read_file("kernel.bin");
engine.machine().attach_disk(read_file("ext2.img"));
engine.boot_cinux(kernel);  // 引导垫片：放置内核、页表、ACPI、CPU 状态
engine.run(400'000'000);    // 到 shell 提示符约 4 亿条（~10 秒 @ 40 Mips）
```

## 快照

```cpp
const auto blob = engine.snapshot();          // 完整机器状态
micro_forge::Engine restored(config);
if (!restored.restore(blob).has_value()) { /* 恢复成功 */ }
```

快照字节格式有版本号，跨引擎版本会被明确拒绝。

## 缓存

`base::SetAssocCache<Key, Value, Capacity, Ways>` 是通用缓存组件：静态数组存储（无堆、构造不失败）、LRU、命中/未命中统计、运行期只能调低 active 容量。decode cache 就是它的使用方（命中率约 94%）。

热路径（TLB）不套它：实测直接映射的 1 次索引比 hash + LRU 快约 20%，组件是给「收益盖过开销」的地方用的。

## 构建与安装

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build --prefix /usr/local
```

装好之后：

```cmake
find_package(micro_forge CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE micro_forge::micro_forge)
```

公开头文件在 `include/micro_forge/`，内部头文件（`src/`）不安装也不应被包含。

## 嵌入式档

```bash
cmake -S . -B build-emb -DMICRO_FORGE_EMBEDDED=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-emb -j
ctest --test-dir build-emb
```

该档加 `-fno-exceptions -fno-rtti` 并定义 `MICRO_FORGE_EMBEDDED=1`。引擎内部本来就只用 `std::expected` 表达失败、不用 `typeid`/`dynamic_cast`，所以这一档在 157 项测试下同样全绿；它的作用是防止将来的代码回退到异常或 RTTI。

## wasm 侧

wasm 产物（`scripts/build-wasm.sh`）导出 C ABI：`mf_instance_create` / `mf_instance_use` / `mf_destroy`、`mf_boot_cinux`、`mf_run`、`mf_read_console`、`mf_inject_serial`、`mf_snapshot` / `mf_restore`。多实例各自独立，默认实例（handle 0）懒创建。`example/wasm/cinux/` 是一个 Vue 3 前端，展示如何把该 ABI 接到终端页面。
