/**
 * @file x86_pc.cpp
 * @brief x86 PC wiring, native execution and portable stop-result adaptation.
 * @version 0.1.0
 */

#include "micro_forge/machines/x86_pc.hpp"

#include "micro_forge/arch/x86_64/cpu.hpp"
#include "micro_forge/base/run_result.hpp"

#include "micro_forge/arch/x86_64/port_space.hpp"
#include "micro_forge/kernel/memory/region.hpp"
#include "micro_forge/kernel/snapshot.hpp"
#include "micro_forge/machines/x86_pc_config.hpp"
#include "micro_forge/version.hpp"

#include <cstdint>
#include <cstdlib>
#include <expected>
#include <optional>
#include <span>
#include <vector>

namespace micro_forge::machines {

X86PcMachine::X86PcMachine(uint64_t ram_size, std::optional<uintptr_t> window)
    : X86PcMachine([ram_size, window] {
          X86PcConfig config;
          config.ram_size = ram_size;
          config.window = window;
          return config;
      }()) {}

X86PcMachine::X86PcMachine(const X86PcConfig& config)
    : ram_(config.window.has_value()
               ? kernel::RamRegion(kernel::RamRegion::WindowBase{*config.window},
                                   config.ram_size + kLfbSize)
               : kernel::RamRegion(config.ram_size + kLfbSize)),
      cpu_(bus_, scheduler_, &ports_), serial_(cpu_.console_sink()),
      ahci_(bus_, disk_, [this] { on_ahci_complete(); }),
      debug_exit_([this](uint32_t encoded) { cpu_.request_debug_exit(encoded); }),
      ram_size_(config.ram_size) {
    // Guest physical 0 is the start of the backing store; the LFB
    // reservation rides its tail as plain memory (spec 5.4). Both maps
    // are layout constants on a fresh bus: unreachable-failure abort,
    // never a side effect inside assert (NDEBUG builds drop it whole).
    if (!bus_.map_ram(0, ram_.size(), ram_).has_value() ||
        !bus_.map(kDebugExitBase, 0x1000, debug_exit_).has_value()) {
        std::abort();
    }
    // Device DMA writes bypass Cpu::store, so self-modifying-code guards
    // would never fire for them; forward bus writes to the invalidator.
    bus_.set_write_notifier([this](uint64_t paddr) { cpu_.note_store(paddr); });
    // Port windows come from the layout: the config decides address, size
    // and presence (size 0 skips the device entirely).
    const auto port_device = [this](X86PcDevice slot) -> arch::x86_64::PortDevice* {
        switch (slot) {
        case X86PcDevice::kSerial:
            return &serial_;
        case X86PcDevice::kPit:
            return &pit_;
        case X86PcDevice::kBoard:
            return &board_;
        case X86PcDevice::kRtc:
            return &rtc_;
        case X86PcDevice::kPost:
            return &post_;
        case X86PcDevice::kFwCfg:
            return &fw_cfg_;
        case X86PcDevice::kPci:
            return &pci_;
        case X86PcDevice::kKbdData:
            return &kbd_data_;
        case X86PcDevice::kKbdStatus:
            return &kbd_status_;
        case X86PcDevice::kPicMaster:
            return &pic_master_;
        case X86PcDevice::kPicSlave:
            return &pic_slave_;
        default:
            return nullptr; // memory-window devices are not port devices
        }
    };
    const auto memory_device = [this](X86PcDevice slot) -> kernel::Region* {
        switch (slot) {
        case X86PcDevice::kAhci:
            return &ahci_;
        case X86PcDevice::kLapic:
            return &lapic_;
        case X86PcDevice::kIoApic:
            return &io_apic_;
        default:
            return nullptr;
        }
    };
    const auto memory_window = [&config](X86PcDevice slot) -> uint64_t {
        for (const X86PcMemoryWindow& window : config.windows) {
            if (window.device == slot) {
                return window.base;
            }
        }
        return 0;
    };
    for (const X86PcPortWindow& window : config.ports) {
        if (window.size == 0) {
            continue;
        }
        arch::x86_64::PortDevice* device = port_device(window.device);
        if (device == nullptr || !ports_.map(window.base, window.size, *device).has_value()) {
            std::abort();
        }
    }
    // AHCI: one SATA disk on port 0 behind mechanism-1 config space.
    if (config.ahci_vendor != 0) {
        ahci_pci_.write(0x00, static_cast<uint32_t>(config.ahci_vendor) |
                                  (static_cast<uint32_t>(config.ahci_device) << 16U));
        ahci_pci_.write(0x08, config.ahci_class);
        const uint64_t ahci_bar = memory_window(X86PcDevice::kAhci);
        ahci_pci_.write(0x24, static_cast<uint32_t>(ahci_bar)); // BAR5 (memory)
        ahci_pci_.write(0x3C, config.ahci_irq);                 // interrupt line
        pci_.add_function(0, 31, 0, ahci_pci_);
    }
    // Device memory windows (AHCI BAR, LAPIC, IO-APIC).
    for (const X86PcMemoryWindow& window : config.windows) {
        if (window.size == 0) {
            continue;
        }
        kernel::Region* region = memory_device(window.device);
        if (region == nullptr || !bus_.map(window.base, window.size, *region).has_value()) {
            std::abort();
        }
    }
    // Size the decode cache and the code-page bitmap to this machine:
    // 512x2 decode entries is the default, RAM pages drive the bitmap.
    cpu_.configure_caches(config.ram_size);
    // Config switch for block compilation; the env var stays as a bypass.
    cpu_.set_jit_enabled(config.jit);
    // The LAPIC timer counts down on the machine's virtual clock.
    lapic_.set_clock([this] { return cpu_.icount(); });
}

uint32_t X86PcMachine::engine_version() {
    return kernel::pack_engine_version(static_cast<unsigned>(kVersionMajor),
                                       static_cast<unsigned>(kVersionMinor),
                                       static_cast<unsigned>(kVersionPatch));
}

bool X86PcMachine::load_image(uint64_t guest_addr, std::span<const uint8_t> image) {
    // Images live inside guest RAM, below the LFB window; copy_in keeps
    // window-mode stores off null-constant pointer arithmetic.
    return ram_.copy_in(guest_addr, image);
}

std::vector<uint8_t> X86PcMachine::snapshot() const {
    kernel::SnapshotWriter writer(engine_version());
    ram_.save(writer);
    cpu_.save(writer);
    scheduler_.save(writer);
    if (snapshot_devices) {
        snapshot_devices(writer);
    }
    return writer.take();
}

std::expected<void, kernel::SnapshotError> X86PcMachine::restore(std::span<const uint8_t> blob) {
    auto reader = kernel::SnapshotReader::open(blob, engine_version());
    if (!reader) {
        return std::unexpected(reader.error());
    }
    if (!ram_.restore(*reader)) {
        return std::unexpected(kernel::SnapshotError::kTruncated);
    }
    if (!cpu_.restore(*reader)) {
        return std::unexpected(kernel::SnapshotError::kTruncated);
    }
    const auto events = scheduler_.restore_begin(*reader);
    if (!events.has_value()) {
        return std::unexpected(kernel::SnapshotError::kTruncated);
    }
    // Each event triple is 24 bytes; a count that cannot fit the unread
    // payload is a corrupt blob, refuse instead of reserve()ing it.
    constexpr uint64_t event_bytes = 24;
    if (*events > reader->remaining() / event_bytes) {
        return std::unexpected(kernel::SnapshotError::kTruncated);
    }
    std::vector<kernel::Scheduler::EventInfo> table;
    table.reserve(static_cast<size_t>(*events));
    for (uint64_t i = 0; i < *events; ++i) {
        auto info = kernel::Scheduler::restore_read_event(*reader);
        if (!info.has_value()) {
            return std::unexpected(kernel::SnapshotError::kTruncated);
        }
        table.push_back(*info);
    }
    if (restore_devices) {
        restore_devices(*reader, table);
    }
    return {};
}

void X86PcMachine::on_ahci_complete() {
    // The kernel routes IRQ11 through the IO-APIC redirection table; the
    // vector it programmed for GSI 11 is what the CPU must receive.
    const auto entry = io_apic_.redirection(11);
    const auto vector = static_cast<uint8_t>(entry[0] & 0xFFU);
    if (vector != 0) {
        cpu_.request_interrupt(vector);
    }
}

arch::x86_64::ExitStatus X86PcMachine::run(uint64_t max_instructions) {
    return cpu_.run(max_instructions);
}

RunResult X86PcMachine::run_for(uint64_t max_instructions) {
    const auto status = run(max_instructions);
    const auto reason = [kind = status.kind] {
        using Kind = arch::x86_64::ExitStatus::Kind;
        switch (kind) {
        case Kind::kRunning:
            return StopReason::kBudgetExhausted;
        case Kind::kHalt:
            return StopReason::kHalted;
        case Kind::kExit:
            return StopReason::kExited;
        case Kind::kDebugExit:
            return StopReason::kDebugExit;
        case Kind::kDecodeError:
            return StopReason::kDecodeError;
        case Kind::kUnsupported:
            return StopReason::kUnsupported;
        case Kind::kFault:
            return StopReason::kFault;
        }
        std::abort();
    }();
    const uint64_t pc = reason == StopReason::kBudgetExhausted ? cpu_.rip() : status.rip;
    return {.reason = reason, .pc = pc, .code = status.code, .detail = status.detail};
}

} // namespace micro_forge::machines
