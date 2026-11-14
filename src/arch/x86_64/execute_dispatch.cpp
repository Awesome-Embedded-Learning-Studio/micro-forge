/**
 * @file execute_dispatch.cpp
 * @brief Top-level execution dispatch: string, syscall, and generic paths.
 * @version 0.1.0
 */

#include "micro_forge/arch/x86_64/cpu.hpp"

#include "micro_forge/arch/x86_64/decoded.hpp"

namespace micro_forge::arch::x86_64 {

ExitStatus Cpu::execute_insn(const DecodedInsn& insn, uint64_t rip_after) {
    switch (insn.mnemonic) {
    case Mnemonic::kMovsb:
    case Mnemonic::kMovsw:
    case Mnemonic::kMovsd:
    case Mnemonic::kMovsq:
    case Mnemonic::kStosb:
    case Mnemonic::kStosw:
    case Mnemonic::kStosd:
    case Mnemonic::kStosq:
    case Mnemonic::kLodsb:
    case Mnemonic::kLodsw:
    case Mnemonic::kLodsd:
    case Mnemonic::kLodsq:
    case Mnemonic::kScasb:
    case Mnemonic::kScasw:
    case Mnemonic::kScasd:
    case Mnemonic::kScasq:
    case Mnemonic::kCmpsb:
    case Mnemonic::kCmpsw:
    case Mnemonic::kCmpsd:
    case Mnemonic::kCmpsq:
        return execute_string(insn, rip_after);
    case Mnemonic::kSyscall:
        return execute_syscall(rip_after);
    default:
        return execute(insn, rip_after);
    }
}

} // namespace micro_forge::arch::x86_64
