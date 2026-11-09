/**
 * @file io.cpp
 * @brief Port I/O execution: in/out routed through the port space (6b).
 * @version 0.1.0
 */

#include "micro_forge/arch/x86_64/cpu.hpp"

namespace micro_forge::arch::x86_64 {

ExitStatus Cpu::execute_ports(const DecodedInsn& insn, uint64_t rip_after) {
    const Operand& a = insn.ops[0];
    const Operand& b = insn.ops[1];
    const auto port_of = [&](const Operand& op) {
        return static_cast<uint16_t>(op.kind == Operand::Kind::kImm ? op.disp
                                                                    : regs_[index_of(Reg::kRdx)]);
    };
    if (insn.mnemonic == Mnemonic::kIn) {
        const auto port = port_of(b);
        uint64_t value = 0;
        if (ports_ == nullptr || !ports_->read(port, a.size, value)) {
            latch(ExitStatus{
                .kind = ExitStatus::Kind::kFault,
                .code = port,
                .rip = insn_rip_,
                .detail = static_cast<uint64_t>(FaultId::kMemory),
            });
            return exit_;
        }
        if (!write_operand(insn, a, value)) {
            return exit_;
        }
        return exit_;
    }
    const auto port = port_of(a);
    uint64_t value = 0;
    if (!read_operand(insn, b, rip_after, value)) {
        return exit_;
    }
    if (ports_ == nullptr || !ports_->write(port, b.size, value)) {
        latch(ExitStatus{
            .kind = ExitStatus::Kind::kFault,
            .code = port,
            .rip = insn_rip_,
            .detail = static_cast<uint64_t>(FaultId::kMemory),
        });
        return exit_;
    }
    return exit_;
}

} // namespace micro_forge::arch::x86_64
