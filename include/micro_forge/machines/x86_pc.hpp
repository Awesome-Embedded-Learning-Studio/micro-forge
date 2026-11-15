/**
 * @file x86_pc.hpp
 * @brief x86 PC assembly: CPU, PC devices, memory layout and snapshot wiring.
 * @version 0.1.0
 *
 * The X86PcMachine owns the wiring that every consumer (native tests, the
 * batch-5 wasm C ABI, later the device banks) needs identically, so the
 * assembly exists exactly once. Memory layout follows the spec 5.4 rule:
 * guest physical 0 is the start of the backing store, and in window mode
 * that store IS the wasm linear memory at offset 0.
 */

#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "micro_forge/arch/x86_64/cpu.hpp"
#include "micro_forge/base/run_result.hpp"
#include "micro_forge/device/pc/ahci.hpp"
#include "micro_forge/device/pc/debug_exit.hpp"
#include "micro_forge/device/pc/fw_cfg.hpp"
#include "micro_forge/device/pc/i8042.hpp"
#include "micro_forge/device/pc/io_apic.hpp"
#include "micro_forge/device/pc/lapic.hpp"
#include "micro_forge/device/pc/pci_config.hpp"
#include "micro_forge/device/pc/pic_8259.hpp"
#include "micro_forge/device/pc/pit_8254.hpp"
#include "micro_forge/device/pc/rtc.hpp"
#include "micro_forge/device/pc/serial_16550.hpp"
#include "micro_forge/generated/machine_config_generated.hpp"
#include "micro_forge/kernel/bus/bus.hpp"
#include "micro_forge/arch/x86_64/port_space.hpp"
#include "micro_forge/device/pc/port_stubs.hpp"
#include "micro_forge/kernel/memory/ram_region.hpp"
#include "micro_forge/kernel/scheduler.hpp"
#include "micro_forge/kernel/snapshot.hpp"
#include "micro_forge/machines/x86_pc_config.hpp"

namespace micro_forge::machines {

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
class X86PcMachine {
public:
    /// LFB reservation: 4 MiB, enough for 1024x768x32 plus slack.
    // Both from machines/*.toml via the generated description.
    static constexpr uint64_t kLfbSize = generated::kLfbSize;
    /// Where the MMIO debug-exit register answers.
    static constexpr uint64_t kDebugExitBase = generated::kDebugExitBase;

    /**
     * @brief Assembles a bare machine: guest RAM plus the debug-exit
     *        register, no devices wired.
     *
     * @param[in] ram_size Guest RAM in bytes (below the LFB window).
     * @param[in] window   Integer base of a non-owning backing of
     *                    ram_size + kLfbSize bytes (wasm: linear offset
     *                    0); nullopt selects the owning vector mode.
     */
    explicit X86PcMachine(uint64_t ram_size, std::optional<uintptr_t> window = std::nullopt);

    /**
     * @brief Assembles a machine from a layout description.
     *
     * The X86PcMachine owns the device instances; the config decides where
     * each one is wired and whether it exists at all (size zero).
     *
     * @param[in] config The layout to wire.
     */
    explicit X86PcMachine(const X86PcConfig& config);

    // Devices retain member references and callbacks capture this assembly.
    X86PcMachine(const X86PcMachine&) = delete;
    X86PcMachine& operator=(const X86PcMachine&) = delete;
    X86PcMachine(X86PcMachine&&) = delete;
    X86PcMachine& operator=(X86PcMachine&&) = delete;

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
    arch::x86_64::ExitStatus run(uint64_t max_instructions);

    /**
     * @brief Executes a budget and translates the stop into the shared facade result.
     * @param[in] max_instructions Instruction budget for the native execution loop.
     * @return Portable stop reason plus the original x86 diagnostic values.
     */
    RunResult run_for(uint64_t max_instructions);

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
     * @return Success, else the envelope refusal reason.
     */
    [[nodiscard]] std::expected<void, kernel::SnapshotError> restore(std::span<const uint8_t> blob);

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
    arch::x86_64::PortSpace ports_;
    arch::x86_64::Cpu cpu_;
    device::pc::Serial16550 serial_;
    device::pc::Pit8254 pit_;
    device::pc::BoardControl board_;
    device::pc::Rtc rtc_;
    device::pc::Pic8259 pic_master_;
    device::pc::Pic8259 pic_slave_;
    device::pc::Lapic lapic_;
    device::pc::IoApic io_apic_;
    device::pc::PciConfig pci_;
    device::pc::PciFunction ahci_pci_;
    device::pc::PostPort post_;
    device::pc::FwCfg fw_cfg_;
    std::vector<uint8_t> disk_;
    device::pc::Ahci ahci_;
    /// Raises the AHCI interrupt through the IO-APIC routing.
    void on_ahci_complete();
    device::pc::I8042State kbd_state_;
    device::pc::I8042 kbd_data_{device::pc::I8042::kData, kbd_state_};
    device::pc::I8042 kbd_status_{device::pc::I8042::kStatus, kbd_state_};
    device::pc::DebugExitRegion debug_exit_;
    uint64_t ram_size_;
};

} // namespace micro_forge::machines
