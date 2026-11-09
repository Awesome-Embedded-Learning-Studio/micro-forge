/**
 * @file exports.cpp
 * @brief The engine's C ABI surface over a single assembled machine.
 * @version 0.1.0
 *
 * Compiled only into the wasm artifact (scripts/build-wasm.sh); the
 * native build never links this translation unit. One static machine:
 * consumers are one-page-one-engine (loupe), and multi-instance needs
 * would bump the ABI version.
 *
 * Memory contract (spec 5.4): the exported linear memory IS guest
 * physical memory from offset 0. Hosts read and write guest memory
 * directly through the memory buffer; no load-image call exists on
 * purpose. Engine statics and heap live above the guest window
 * (global-base), so no host-visible guest address ever aliases engine
 * state.
 *
 * Addresses in this ABI are uint32: wasm32 linear offsets cover the
 * whole 4 GiB guest physical space by construction.
 */

#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#include "micro_forge/arch/x86_64/decoder.hpp"
#include "micro_forge/base/wasm_block.hpp"
#include "micro_forge/base/wasm_emit.hpp"
#include "micro_forge/loader/cinux_loader.hpp"
#include "micro_forge/machine.hpp"
#include "micro_forge/presets/cinux_machine.hpp"
#include "micro_forge/version.hpp"

// ---------------------------------------------------------------------------
// ABI contract (v1). Every convention consumers depend on, in one place:
//   - mf_run packs the exit as: kind in the low byte (Cpu ExitStatus
//     ordinals, pinned by the static_asserts below), code in bits 8+.
//   - mf_restore returns 0 on success, else SnapshotError ordinal + 1.
//   - mf_snapshot returns the needed length when cap is too small and
//     writes nothing; a successful call returns the bytes written.
//   - Addresses are uint32 linear-memory offsets (the 4 GiB envelope).
//   - The memory layout (RAM size, LFB base/size) is a property of the
//     artifact: query it, do not assume it (the harnesses derive every
//     expectation from these exports).
// An enum reorder below changes the wire contract: bump mf_abi_version.
// ---------------------------------------------------------------------------
static_assert(static_cast<int>(micro_forge::arch::x86_64::ExitStatus::Kind::kRunning) == 0);
static_assert(static_cast<int>(micro_forge::arch::x86_64::ExitStatus::Kind::kHalt) == 1);
static_assert(static_cast<int>(micro_forge::arch::x86_64::ExitStatus::Kind::kExit) == 2);
static_assert(static_cast<int>(micro_forge::arch::x86_64::ExitStatus::Kind::kDebugExit) == 3);
static_assert(static_cast<int>(micro_forge::arch::x86_64::ExitStatus::Kind::kDecodeError) == 4);
static_assert(static_cast<int>(micro_forge::arch::x86_64::ExitStatus::Kind::kUnsupported) == 5);
static_assert(static_cast<int>(micro_forge::arch::x86_64::ExitStatus::Kind::kFault) == 6);
// The build recipe's 0x400000 LFB constant and this artifact's layout
// must stay one number (batch-5 review F3).
static_assert(micro_forge::Machine::kLfbSize == 0x400000);

// The wasm recipe always defines GUEST_RAM_MB explicitly. The default
// below only serves static analysis runs that compile this file without
// the recipe (clang-tidy has no build entry for it).
#ifndef GUEST_RAM_MB
#    define GUEST_RAM_MB 512
#endif

namespace {

constexpr uint64_t kGuestRam = static_cast<uint64_t>(GUEST_RAM_MB) << 20U;

// All guest-memory traffic from this surface goes through the machine's
// RAM region helpers (copy_in / view): they carry the integer-base
// discipline that keeps window-mode stores away from null-constant
// pointer arithmetic, which the optimizer is free to delete.

// Instance table: one Machine per handle, current() is the one the legacy
// exports act on. Instance 0 stays the lazily created default so the older
// single-machine ABI keeps working unchanged.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::vector<std::unique_ptr<micro_forge::Machine>> g_instances;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::size_t g_current = 0;

/// Creates the default instance on first use (the legacy ABI's machine).
micro_forge::Machine& current() {
    if (g_instances.empty()) {
        g_instances.push_back(std::make_unique<micro_forge::Machine>(kGuestRam, uintptr_t{0}));
    }
    return *g_instances[g_current];
}

/// Packs an exit status into the u64 the ABI returns: kind in the low
/// byte, code in the upper 56.
uint64_t pack_exit(const micro_forge::arch::x86_64::ExitStatus& status) {
    return (status.code << 8U) | static_cast<uint64_t>(status.kind);
}

} // namespace

extern "C" {

/// @brief Creates a machine instance over the guest window.
///
/// @param[in] ram_size Usable guest RAM in bytes (0 selects the default).
/// @param[in] window Host window base (0 selects the default window).
/// @return A handle >= 1, or 0 when allocation failed.
uint32_t mf_instance_create(uint32_t ram_size, uint32_t window) {
    const uint64_t ram = ram_size == 0 ? kGuestRam : ram_size;
    auto instance = std::make_unique<micro_forge::Machine>(
        micro_forge::cinux_machine_config(ram, static_cast<uintptr_t>(window)));
    g_instances.push_back(std::move(instance));
    return static_cast<uint32_t>(g_instances.size()); // handle = index + 1
}

/// @brief Destroys an instance; the default instance is never destroyed.
/// @param[in] handle Handle from mf_instance_create.
void mf_instance_destroy(uint32_t handle) {
    if (handle == 0 || handle > g_instances.size() || handle - 1 == 0) {
        return; // default instance lives for the module
    }
    g_instances[handle - 1].reset();
}

/// @brief Selects the instance the legacy exports act on.
/// @param[in] handle Handle from mf_instance_create (0 = default).
/// @return 1 on success, 0 for an unknown handle.
uint32_t mf_instance_use(uint32_t handle) {
    const std::size_t index = handle == 0 ? 0 : handle - 1;
    if (index >= g_instances.size() && index != 0) {
        return 0;
    }
    (void)current(); // ensure the default exists
    if (index >= g_instances.size()) {
        return 0;
    }
    g_current = index;
    return 1;
}

/// @brief ABI version of this export surface; bumps on breaking changes.
uint32_t mf_abi_version(void) {
    return 1;
}

/// @brief Packed engine version (major<<16 | minor<<8 | patch).
uint32_t mf_engine_version(void) {
    return micro_forge::kernel::pack_engine_version(
        static_cast<unsigned>(micro_forge::kVersionMajor),
        static_cast<unsigned>(micro_forge::kVersionMinor),
        static_cast<unsigned>(micro_forge::kVersionPatch));
}

/// @brief Snapshot format version this engine writes and reads.
uint32_t mf_snapshot_format_version(void) {
    return micro_forge::kernel::kSnapshotFormatVersion;
}

/// @brief Guest RAM size of this artifact in bytes (the memory layout is
///        a property of the artifact, not of the instance).
uint32_t mf_guest_ram_size(void) {
    return static_cast<uint32_t>(kGuestRam);
}

/// @brief Guest-physical base of the LFB reservation.
uint32_t mf_lfb_base(void) {
    return static_cast<uint32_t>(current().lfb_base());
}

/// @brief LFB reservation size in bytes (geometry lands in batch 8).
uint32_t mf_lfb_size(void) {
    return static_cast<uint32_t>(micro_forge::Machine::kLfbSize);
}

/// @brief Resets the machine to an entry point (clears CPU state,
///        console, latched exit; memory keeps its contents).
void mf_reset(uint32_t entry, uint32_t stack_top) {
    current().boot(entry, stack_top);
}

/// @brief Runs up to @p max_instructions instructions.
/// @return Packed exit: kind in the low byte (Cpu ExitStatus order),
///         exit code in the upper bits.
uint64_t mf_run(uint64_t max_instructions) {
    return pack_exit(current().run(max_instructions));
}

/// @brief Boots a Cinux kernel image with an ext2 root volume.
///
/// The two blobs live in the wasm linear memory at the caller's arena
/// (allocate with the exported malloc). The boot shim validates the
/// image header, places the image, builds the page tables and hands
/// control to the kernel.
///
/// @param[in] kernel_ptr Host pointer of the kernel.bin bytes.
/// @param[in] kernel_len kernel.bin length.
/// @param[in] disk_ptr Host pointer of the ext2.img bytes.
/// @param[in] disk_len ext2.img length.
/// @return 1 on success, 0 when the boot shim refused the image.
uint32_t mf_boot_cinux(uint32_t kernel_ptr, uint32_t kernel_len, uint32_t disk_ptr,
                       uint32_t disk_len) {
    // NOLINTNEXTLINE(performance-no-int-to-ptr) -- wasm ABI hands out linear offsets
    const auto* kernel = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(kernel_ptr));
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    const auto* disk = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(disk_ptr));
    const std::span<const uint8_t> image{kernel, kernel_len};
    std::vector<uint8_t> volume(disk, disk + disk_len);
    current().attach_disk(std::move(volume));
    return micro_forge::loader::boot_cinux(current().ram(), current().cpu(), image).has_value()
               ? 1U
               : 0U;
}

/// @brief Queues one byte on the COM1 receiver (typed input).
/// @param[in] byte The byte as typed on the serial line.
void mf_inject_serial(uint32_t byte) {
    current().inject_serial(static_cast<uint8_t>(byte));
}

/// @brief Guest RIP (diagnostic: where the guest is executing).
uint32_t mf_rip_low(void) {
    return static_cast<uint32_t>(current().cpu().rip());
}

/// @brief Guest RIP high 32 bits.
uint32_t mf_rip_high(void) {
    return static_cast<uint32_t>(current().cpu().rip() >> 32U);
}

/// @brief Bytes the guest wrote to COM1 THR (diagnostic).
uint64_t mf_serial_tx(void) {
    return current().serial_tx_count();
}

/// @brief Guest-physical 32-bit read (diagnostic).
uint32_t mf_peek32(uint32_t paddr) {
    const auto bytes = current().ram().view(paddr, 4);
    if (bytes.size() != 4) {
        return 0;
    }
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8U) |
           (static_cast<uint32_t>(bytes[2]) << 16U) | (static_cast<uint32_t>(bytes[3]) << 24U);
}

/// @brief Retired instruction count (virtual time).
uint64_t mf_icount(void) {
    return current().cpu().icount();
}

/// @brief Copies the serial console out to a wasm linear-memory address.
///
/// @p dst is a host pointer (malloc arena), NOT a guest physical address:
/// the console lives in engine memory outside the guest window, and the
/// embedder reads it through an ordinary wasm pointer.
///
/// @return Bytes copied (the whole console when it fits).
uint64_t mf_read_console(uint32_t dst, uint32_t cap) {
    const std::span<const char> console = current().console();
    const uint64_t n = std::min<uint64_t>(console.size(), cap);
    if (n != 0) {
        // NOLINTNEXTLINE(performance-no-int-to-ptr) -- dst is a malloc'd wasm pointer
        std::memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(dst)), console.data(),
                    static_cast<std::size_t>(n));
    }
    return n;
}

/// @brief Writes a snapshot blob into guest memory at @p dst.
/// @return Bytes written, or the needed length when @p cap is too small
///         (nothing is written in that case; the host retries).
uint64_t mf_snapshot(uint32_t dst, uint32_t cap) {
    const std::vector<uint8_t> blob = current().snapshot();
    if (blob.size() > cap) {
        return blob.size();
    }
    if (!current().ram().copy_in(dst, blob)) {
        return 0;
    }
    return blob.size();
}

/// @brief Copies bytes into guest physical RAM.
/// @param[in] paddr Destination guest-physical address.
/// @param[in] src Source pointer (wasm linear memory).
/// @param[in] len Byte count.
/// @return 1 on success, 0 when the range does not fit.
uint32_t mf_write_guest(uint32_t paddr, uint32_t src, uint32_t len) {
    // In wasm32 a linear-memory address equals the byte offset.
    // NOLINTNEXTLINE(performance-no-int-to-ptr) -- wasm32 offsets are pointers
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(src));
    return current().ram().copy_in(paddr, std::span<const std::uint8_t>(bytes, len)) ? 1 : 0;
}

/// @brief Compiles the guest block at @p rip into a wasm module.
///
/// Returns a malloc'd module (the caller frees it) and writes its length
/// to @p out_len; 0 when the block cannot be lowered (the caller must
/// fall back to the interpreter).
/// @param[in] rip_lo Low 32 bits of the guest block address.
/// @param[in] rip_hi High 32 bits of the guest block address.
/// @param[in] max_insns Upper bound on instructions per block.
/// @param[out] out_len Module byte length.
/// @return Pointer to the module bytes, or 0.
uint32_t mf_wasm_block(uint32_t rip_lo, uint32_t rip_hi, uint32_t max_insns, uint32_t* out_len) {
    const uint64_t rip = (static_cast<uint64_t>(rip_hi) << 32U) | rip_lo;
    using namespace micro_forge;
    *out_len = 0;
    std::vector<arch::x86_64::DecodedInsn> insns;
    uint64_t pc = rip;
    for (uint32_t step = 0; step < max_insns; ++step) {
        // rip is taken as a guest-physical address here: the page-side
        // debug path writes test programs into RAM directly and does not
        // want to depend on the kernel's page tables.
        // Low 32 bits of the block address are the physical address on
        // this debug path (the guest program was written at a low paddr).
        const auto paddr = static_cast<uint64_t>(static_cast<std::uint32_t>(pc));
        const auto bytes = current().ram().view(paddr, 16);
        if (bytes.size() != 16) {
            *out_len = 0x102; // physical view out of range
            return 0;
        }
        const auto decoded = arch::x86_64::decode_one(bytes);
        if (!decoded) {
            *out_len = 0x103; // decode refused the bytes
            return 0;
        }
        if (decoded->mnemonic == arch::x86_64::Mnemonic::kRet) {
            break;
        }
        if (decoded->length == 0) {
            break;
        }
        insns.push_back(*decoded);
        pc += decoded->length;
    }
    if (insns.empty()) {
        *out_len = 0x104; // no instruction lowered
        return 0;
    }
    std::vector<std::uint8_t> body;
    const auto lowered = base::append_wasm_block(insns, body);
    if (lowered == 0) {
        *out_len = 0x105; // append_wasm_block produced nothing
        return 0;
    }
    base::WasmModule module;
    module.add_memory(1);
    static constexpr std::array<base::WasmModule::ValType, 0> no_params = {};
    static constexpr std::array<base::WasmModule::ValType, 0> no_results = {};
    const auto index = module.add_function(no_params, no_results, body);
    module.export_function("block", index);
    module.export_memory("memory");
    const auto bytes = module.bytes();
    auto* buffer = static_cast<std::uint8_t*>(std::malloc(bytes.size()));
    if (buffer == nullptr) {
        *out_len = 0x106; // allocator refused
        return 0;
    }
    std::memcpy(buffer, bytes.data(), bytes.size());
    *out_len = static_cast<std::uint32_t>(bytes.size());
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(buffer));
}

/// @brief Restores the machine from a blob at @p src.
/// @return 0 on success, else the SnapshotError ordinal + 1.
uint32_t mf_restore(uint32_t src, uint32_t len) {
    const auto blob = current().ram().view(src, len);
    if (blob.size() != len) {
        return static_cast<uint32_t>(micro_forge::kernel::SnapshotError::kTruncated) + 1U;
    }
    const auto refused = current().restore(blob);
    return refused.has_value() ? static_cast<uint32_t>(*refused) + 1U : 0U;
}
}
