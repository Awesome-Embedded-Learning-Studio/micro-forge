/**
 * @file memory.cpp
 * @brief Data access: inline TLB hits and precise fault fallbacks.
 * @version 0.1.0
 */

#include "micro_forge/arch/x86_64/cpu.hpp"
#include "micro_forge/kernel/le_access.hpp"

namespace micro_forge::arch::x86_64 {

bool Cpu::load(uint64_t address, unsigned width, uint64_t& out) {
    uint64_t paddr = 0;
    if (!mmu_.lookup(address, false, false, cpl_ == 3, cr_[0], efer_value_, paddr)) {
        const auto translated = translate_access(address, false, false, insn_rip_);
        if (!translated.ok && exception_injected_) {
            exception_injected_ = false;
            return false;
        }
        if (!translated.ok) {
            latch(ExitStatus{
                .kind = ExitStatus::Kind::kFault,
                .code = address,
                .rip = insn_rip_,
                .detail = static_cast<uint64_t>(FaultId::kMemory),
            });
            return false;
        }
        paddr = translated.paddr;
    }
    address = paddr;
    uintptr_t host = 0;
    const unsigned bytes = width == 1 || width == 2 || width == 4 ? width : 8;
    if (bus_.direct_ram(address, bytes, host)) {
        // Integer arithmetic before the cast keeps wasm address 0 valid.
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        const auto* data = reinterpret_cast<const uint8_t*>(host);
        switch (width) {
        case 1:
            out = *data;
            break;
        case 2:
            out = kernel::get_le16(data);
            break;
        case 4:
            out = kernel::get_le32(data);
            break;
        default:
            out = kernel::get_le64(data);
            break;
        }
        return true;
    }
    switch (width) {
    case 1: {
        const auto r = bus_.read_u8(address);
        if (!r) {
            break;
        }
        out = *r;
        return true;
    }
    case 2: {
        const auto r = bus_.read_u16(address);
        if (!r) {
            break;
        }
        out = *r;
        return true;
    }
    case 4: {
        const auto r = bus_.read_u32(address);
        if (!r) {
            break;
        }
        out = *r;
        return true;
    }
    default: {
        const auto r = bus_.read_u64(address);
        if (!r) {
            break;
        }
        out = *r;
        return true;
    }
    }
    latch(ExitStatus{
        .kind = ExitStatus::Kind::kFault,
        .code = address,
        .rip = insn_rip_,
        .detail = static_cast<uint64_t>(FaultId::kMemory),
    });
    return false;
}

bool Cpu::store(uint64_t address, unsigned width, uint64_t value) {
    uint64_t paddr = 0;
    if (!mmu_.lookup(address, true, false, cpl_ == 3, cr_[0], efer_value_, paddr)) {
        const auto translated = translate_access(address, true, false, insn_rip_);
        if (!translated.ok && exception_injected_) {
            exception_injected_ = false;
            return false;
        }
        if (!translated.ok) {
            latch(ExitStatus{
                .kind = ExitStatus::Kind::kFault,
                .code = address,
                .rip = insn_rip_,
                .detail = static_cast<uint64_t>(FaultId::kMemory),
            });
            return false;
        }
        paddr = translated.paddr;
    }
    address = paddr;
    note_store(address); // self-modifying code invalidates cached decodes
    uintptr_t host = 0;
    const unsigned bytes = width == 1 || width == 2 || width == 4 ? width : 8;
    if (bus_.direct_ram(address, bytes, host)) {
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        auto* data = reinterpret_cast<uint8_t*>(host);
        switch (width) {
        case 1:
            *data = static_cast<uint8_t>(value);
            break;
        case 2:
            kernel::put_le16(data, static_cast<uint16_t>(value));
            break;
        case 4:
            kernel::put_le32(data, static_cast<uint32_t>(value));
            break;
        default:
            kernel::put_le64(data, value);
            break;
        }
        return true;
    }
    std::expected<void, kernel::BusError> r;
    switch (width) {
    case 1:
        r = bus_.write_u8(address, static_cast<uint8_t>(value));
        break;
    case 2:
        r = bus_.write_u16(address, static_cast<uint16_t>(value));
        break;
    case 4:
        r = bus_.write_u32(address, static_cast<uint32_t>(value));
        break;
    default:
        r = bus_.write_u64(address, value);
        break;
    }
    if (r.has_value()) {
        return true;
    }
    latch(ExitStatus{
        .kind = ExitStatus::Kind::kFault,
        .code = address,
        .rip = insn_rip_,
        .detail = static_cast<uint64_t>(FaultId::kMemory),
    });
    return false;
}

} // namespace micro_forge::arch::x86_64
