/**
 * @file cached_insn.hpp
 * @brief Interpreter-only execution plans for decoded cache entries.
 * @version 0.1.0
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "micro_forge/arch/x86_64/decoded.hpp"

namespace micro_forge::arch::x86_64 {

/// Validated common operand forms; everything else uses the full executor.
enum class ExecOp : uint8_t {
    kGeneric,
    kMov64,
    kMov32,
    kImm64,
    kImm32,
    kLoad64,
    kLoad32,
    kLoad16,
    kLoad8,
    kStore64,
    kStore32,
    kStore16,
    kStore8,
    kStoreImm,
    kLea64,
    kLea32,
    kAlu64,
    kInc64,
    kDec64,
    kJe,
    kJne,
    kJcc,
    kJmp,
};

/// Decoded instruction plus a plan selected once on cache insertion.
struct CachedInsn {
    DecodedInsn insn{};
    ExecOp op = ExecOp::kGeneric;
    /// Address-space fingerprint (CR3) at insertion; a vaddr-keyed hit
    /// under a different CR3 would decode another process's bytes.
    uint64_t cr3 = 0;
};

/// Avoid libc++'s general byte hash for uint64_t lookups on wasm32.
struct DecodeHash {
    [[nodiscard]] std::size_t operator()(uint64_t address) const {
        const auto folded = static_cast<uint32_t>(address ^ (address >> 32U));
        return folded ^ (folded >> 12U);
    }
};

/// @brief Prepares a cache entry. @param[in] insn Decoded instruction.
/// @return Instruction with a validated execution plan.
[[nodiscard]] CachedInsn cache_insn(const DecodedInsn& insn);

} // namespace micro_forge::arch::x86_64
