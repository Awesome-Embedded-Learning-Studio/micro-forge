/**
 * @file machine.hpp
 * @brief Assembled guest machine: bus, memory, CPU, debug-exit, snapshot.
 * @version 0.1.0
 *
 * The Machine owns the wiring that every consumer (native tests, the
 * batch-5 wasm C ABI, later the device banks) needs identically, so the
 * assembly exists exactly once. Memory layout follows the spec 5.4 rule:
 * guest physical 0 is the start of the backing store, and in window mode
 * that store IS the wasm linear memory at offset 0.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "micro_forge/arch/x86_64/cpu.hpp"
#include "micro_forge/base/machine_config.hpp"
#include "micro_forge/kernel/ahci.hpp"
#include "micro_forge/kernel/bus.hpp"
#include "micro_forge/kernel/debug_exit.hpp"
#include "micro_forge/kernel/fw_cfg.hpp"
#include "micro_forge/kernel/i8042.hpp"
#include "micro_forge/kernel/io_apic.hpp"
#include "micro_forge/kernel/lapic.hpp"
#include "micro_forge/kernel/pci_config.hpp"
#include "micro_forge/kernel/pic_8259.hpp"
#include "micro_forge/kernel/pit_8254.hpp"
#include "micro_forge/kernel/port_space.hpp"
#include "micro_forge/kernel/port_stubs.hpp"
#include "micro_forge/kernel/ram_region.hpp"
#include "micro_forge/kernel/rtc.hpp"
#include "micro_forge/kernel/scheduler.hpp"
#include "micro_forge/kernel/serial_16550.hpp"
#include "micro_forge/kernel/snapshot.hpp"

namespace micro_forge {

/**
 * @brief One assembled guest machine over the batch-3/4 components.
 *
 * Guest-physical map (Bus is the routing authority):
 * - [0, ram_size): guest RAM
 * - [ram_size, ram_size + kLfbSize): LFB reservation -- plain memory
 *   (spec: 普通内存当 LFB), address published as lfb_base(), geometry
 *   lands with batch 8
 * - kDebugExitBase: MMIO debug-exit, not memory-backed
 *
 * Backing modes: owning (std::vector, native default) or window
 * (caller-owned bytes, e.g. the wasm linear memory; spec 5.4's "guest
 * physical 0 equals offset 0" rule).
 */
class Machine {
public:
    /// LFB reservation: 4 MiB, enough for 1024x768x32 plus slack.
    static constexpr uint64_t kLfbSize = 0x400000;
    /// Where the MMIO debug-exit register answers.
    static constexpr uint64_t kDebugExitBase = 0xFEED0000;

    /**
     * @brief Assembles the machine with @p ram_size bytes of guest RAM
     *        (the LFB reservation is appended past it).
     *
     * @param[in] ram_size Guest RAM in bytes (below the LFB window).
     * @param[in] window   Integer base of a non-owning backing of
     *                    ram_size + kLfbSize bytes (wasm: linear offset
     *                    0); nullopt selects the owning vector mode.
     */
    explicit Machine(uint64_t ram_size, std::optional<uintptr_t> window = std::nullopt);

    /**
     * @brief Assembles a machine from a layout description.
     *
     * The Machine owns the device instances; the config decides where
     * each one is wired and whether it exists at all (size zero).
     *
     * @param[in] config The layout to wire.
     */
    explicit Machine(const MachineConfig& config);

    /// @brief The CPU (execution, registers, trace hook, irq hook).
    [[nodiscard]] arch::x86_64::Cpu& cpu() noexcept { return cpu_; }
    /// @brief The CPU, const flavor.
    [[nodiscard]] const arch::x86_64::Cpu& cpu() const noexcept { return cpu_; }
    /// @brief The guest bus (extra devices map here).
    [[nodiscard]] kernel::Bus& bus() noexcept { return bus_; }
    /// @brief The scheduler (timed devices register here).
    [[nodiscard]] kernel::Scheduler& scheduler() noexcept { return scheduler_; }
    /// @brief Guest RAM including the LFB reservation.
    [[nodiscard]] kernel::RamRegion& ram() noexcept { return ram_; }
    /// @brief Guest RAM size in bytes (LFB window excluded).
    [[nodiscard]] uint64_t ram_size() const noexcept { return ram_size_; }

    /// @brief Bytes the guest has written to COM1 THR (diagnostic).
    [[nodiscard]] uint64_t serial_tx_count() const { return serial_.tx_count(); }
    /// @brief Queues one byte on the COM1 receiver.
    /// @param[in] byte The byte as typed on the serial line.
    void inject_serial(uint8_t byte) { serial_.inject_rx(byte); }
    /// @brief Attaches the SATA disk image (the AHCI port 0 volume).
    /// @param[in] image Host bytes of the disk; empty detaches.
    void attach_disk(std::vector<uint8_t> image) { disk_ = std::move(image); }
    /// @brief Guest-physical base of the LFB reservation.
    [[nodiscard]] uint64_t lfb_base() const noexcept { return ram_size_; }

    /**
     * @brief Copies an image into guest memory.
     *
     * @param[in] guest_addr Destination guest-physical address; must
     *            keep the whole image inside guest RAM.
     * @param[in] image Image bytes.
     * @return Success; false when the span leaves guest RAM.
     */
    [[nodiscard]] bool load_image(uint64_t guest_addr, std::span<const uint8_t> image);

    /**
     * @brief Resets the machine to an entry point.
     *
     * @param[in] entry First rip.
     * @param[in] stack_top Initial rsp.
     */
    void boot(uint64_t entry, uint64_t stack_top) { cpu_.reset(entry, stack_top); }

    /**
     * @brief Runs the CPU.
     *
     * @param[in] max_instructions Instruction budget.
     * @return The terminal exit status (see Cpu::run).
     */
    [[nodiscard]] arch::x86_64::ExitStatus run(uint64_t max_instructions) {
        return cpu_.run(max_instructions);
    }

    /// @brief Bytes collected by the write syscall so far.
    [[nodiscard]] std::span<const char> console() const {
        return {cpu_.console().data(), cpu_.console().size()};
    }

    /**
     * @brief Serializes the machine: RAM, then CPU, then scheduler, under
     *        the engine's own version triple.
     *
     * Timed devices re-register through the snapshot hooks below; a
     * machine with no devices snapshots an empty event table.
     *
     * @return The sealed blob.
     */
    [[nodiscard]] std::vector<uint8_t> snapshot() const;

    /**
     * @brief Replaces this machine's contents from @p blob.
     *
     * @param[in] blob A snapshot from an identically-laid-out machine.
     * @return Nothing on success, else the envelope refusal reason.
     */
    [[nodiscard]] std::optional<kernel::SnapshotError> restore(std::span<const uint8_t> blob);

    /// Section order in the blob: RAM, CPU, scheduler, then devices.
    /// The snapshot hook appends device sections; the restore hook
    /// receives the scheduler's serialized event table (so a device bank
    /// can re-register its callbacks via restore_event) followed by the
    /// positioned reader at the device sections. Null hooks accept
    /// machines without timed devices (batch-5 reality).
    std::function<void(kernel::SnapshotWriter& writer)> snapshot_devices;
    std::function<void(kernel::SnapshotReader& reader,
                       std::span<const kernel::Scheduler::EventInfo> events)>
        restore_devices;

private:
    /// Packed engine version for the snapshot triple.
    [[nodiscard]] static uint32_t engine_version();

    kernel::Bus bus_;
    kernel::RamRegion ram_;
    kernel::Scheduler scheduler_;
    kernel::PortSpace ports_;
    arch::x86_64::Cpu cpu_;
    kernel::Serial16550 serial_;
    kernel::Pit8254 pit_;
    kernel::BoardControl board_;
    kernel::Rtc rtc_;
    kernel::Pic8259 pic_master_;
    kernel::Pic8259 pic_slave_;
    kernel::Lapic lapic_;
    kernel::IoApic io_apic_;
    kernel::PciConfig pci_;
    kernel::PciFunction ahci_pci_;
    kernel::PostPort post_;
    kernel::FwCfg fw_cfg_;
    std::vector<uint8_t> disk_;
    kernel::Ahci ahci_;
    /// Raises the AHCI interrupt through the IO-APIC routing.
    void on_ahci_complete();
    kernel::I8042State kbd_state_;
    kernel::I8042 kbd_data_{kernel::I8042::kData, kbd_state_};
    kernel::I8042 kbd_status_{kernel::I8042::kStatus, kbd_state_};
    kernel::DebugExitRegion debug_exit_;
    uint64_t ram_size_;
};

} // namespace micro_forge
