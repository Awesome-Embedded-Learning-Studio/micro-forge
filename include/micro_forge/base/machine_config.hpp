/**
 * @file machine_config.hpp
 * @brief Data-driven machine layout: which device answers which window.
 * @version 0.1.0
 *
 * The Machine owns the devices; this header owns the *wiring*. A layout
 * is a list of port windows and memory windows plus the RAM size, so the
 * same engine can present Cinux, Linux, or a test bench without editing
 * the constructor. `cinux_machine_config()` is the preset that reproduces
 * the historical hard-coded layout exactly.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace micro_forge {

/// @brief Devices the machine can wire. One slot per device instance the
///        Machine owns today; the enum keeps the config free of pointers.
/**
 * @brief Devices the machine can wire (one slot per owned instance).
 *
 * The enum keeps the layout free of pointers: a config names the device
 * kind, the Machine resolves it to its own instance when wiring.
 */
enum class MachineDevice : uint8_t {
    kSerial,    ///< COM1 16550 UART.
    kPit,       ///< 8254 programmable interval timer.
    kBoard,     ///< Port 0x61 board control (speaker gate, refresh bits).
    kRtc,       ///< CMOS real-time clock.
    kPost,      ///< Port 0x80 POST latch.
    kFwCfg,     ///< QEMU fw_cfg data/selector pair.
    kPci,       ///< PCI mechanism-1 config address/data.
    kKbdData,   ///< 8042 data port.
    kKbdStatus, ///< 8042 status/command port.
    kPicMaster, ///< 8259 master PIC.
    kPicSlave,  ///< 8259 slave PIC.
    kAhci,      ///< AHCI controller register block (memory window).
    kLapic,     ///< Local APIC page (memory window).
    kIoApic,    ///< IO-APIC page (memory window).
};

/// @brief One port-space window.
struct PortWindow {
    uint16_t base = 0; ///< First I/O port.
    uint16_t size = 0; ///< Window length; zero disables the device.
    MachineDevice device = MachineDevice::kSerial;
};

/// @brief One guest-physical window served by a device.
struct MemoryWindow {
    uint64_t base = 0; ///< First guest-physical address.
    uint64_t size = 0; ///< Window length; zero disables the device.
    MachineDevice device = MachineDevice::kAhci;
};

/// @brief The full machine layout. RAM is always [0, ram_size + LFB).
struct MachineConfig {                 // NOLINT(cppcoreguidelines-pro-type-member-init)
    uint64_t ram_size = 0x10000000;    ///< Usable guest RAM in bytes.
    std::optional<uintptr_t> window;   ///< Host window base (wasm); null = owning.
    std::vector<PortWindow> ports;     ///< Port windows.
    std::vector<MemoryWindow> windows; ///< Device memory windows.
    /// AHCI PCI identity: the function is registered behind mechanism-1
    /// config space; zero vendor disables the function entirely.
    uint16_t ahci_vendor = 0x8086;
    uint16_t ahci_device = 0x2922;
    uint32_t ahci_class = 0x01060100U;
    uint8_t ahci_irq = 11;
    /// Compile guest blocks to native code as they warm up. The
    /// MICRO_FORGE_JIT environment variable turns this on regardless of
    /// the config (probes and differential tests rely on that bypass).
    bool jit = false;
};

} // namespace micro_forge
