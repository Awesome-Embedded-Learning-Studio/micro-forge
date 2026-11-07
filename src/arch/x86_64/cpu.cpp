/**
 * @file cpu.cpp
 * @brief CPU core loop: fetch/decode/step, syscall surface, string ops.
 * @version 0.1.0
 */

#include "micro_forge/arch/x86_64/cpu.hpp"

#include "micro_forge/arch/x86_64/decoder.hpp"

#include <expected>
#include <limits>
#include <span>

namespace micro_forge::arch::x86_64 {

void Cpu::reset(uint64_t rip, uint64_t rsp) {
    regs_.fill(0);
    rip_ = rip;
    insn_rip_ = rip;
    regs_[index_of(Reg::kRsp)] = rsp;
    cf_ = false;
    pf_ = false;
    af_ = false;
    zf_ = true;
    sf_ = false;
    of_ = false;
    df_ = false;
    icount_ = 0;
    console_.clear();
    exit_ = ExitStatus{};
}

void Cpu::request_debug_exit(uint32_t encoded_code) {
    exit_ = ExitStatus{
        .kind = ExitStatus::Kind::kDebugExit,
        .code = encoded_code,
        .rip = insn_rip_,
        .detail = 0,
    };
}

bool Cpu::latch(ExitStatus status) {
    if (status.kind != ExitStatus::Kind::kRunning && exit_.kind == ExitStatus::Kind::kRunning) {
        exit_ = status;
    }
    return exit_.kind != ExitStatus::Kind::kRunning;
}

void Cpu::retire(unsigned n) {
    for (unsigned i = 0; i < n; ++i) {
        ++icount_;
        scheduler_.dispatch(icount_);
    }
    if (trace_) {
        trace_(insn_rip_);
    }
}

bool Cpu::fetch(unsigned& got) {
    got = 0;
    while (got < fetch_buf_.size()) {
        const auto byte = bus_.read_u8(rip_ + got);
        if (!byte) {
            // A short fetch is fine when the instruction fits in what we
            // got; decode reports kTruncated otherwise.
            return got != 0;
        }
        fetch_buf_[got] = *byte;
        ++got;
    }
    return true;
}

ExitStatus Cpu::step() {
    if (exit_.kind != ExitStatus::Kind::kRunning) {
        return exit_;
    }
    unsigned got = 0;
    if (!fetch(got)) {
        latch(ExitStatus{
            .kind = ExitStatus::Kind::kFault,
            .code = rip_,
            .rip = rip_,
            .detail = static_cast<uint64_t>(FaultId::kFetch),
        });
        return exit_;
    }
    const auto decoded = decode_one(std::span<const uint8_t>(fetch_buf_.data(), got));
    if (!decoded) {
        latch(ExitStatus{
            .kind = ExitStatus::Kind::kDecodeError,
            .code = rip_,
            .rip = rip_,
            .detail = static_cast<uint64_t>(decoded.error().kind),
        });
        return exit_;
    }
    insn_rip_ = rip_;
    cur_length_ = decoded->length;
    const uint64_t rip_after = rip_ + decoded->length;
    switch (decoded->mnemonic) {
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
        return execute_string(*decoded, rip_after);
    case Mnemonic::kSyscall:
        return execute_syscall();
    default:
        return execute(*decoded, rip_after);
    }
}

ExitStatus Cpu::run(uint64_t max_instructions) {
    const kernel::Tick start = icount_;
    while (exit_.kind == ExitStatus::Kind::kRunning && icount_ - start < max_instructions) {
        step();
    }
    return exit_;
}

ExitStatus Cpu::unsupported(const DecodedInsn& insn) {
    latch(ExitStatus{
        .kind = ExitStatus::Kind::kUnsupported,
        .code = insn_rip_,
        .rip = insn_rip_,
        .detail = static_cast<uint64_t>(insn.mnemonic),
    });
    return exit_;
}

bool Cpu::push_value(uint64_t value, unsigned width) {
    const unsigned sp_index = index_of(Reg::kRsp);
    const uint64_t sp = regs_[sp_index] - width;
    if (!store(sp, width, value)) {
        return false;
    }
    regs_[sp_index] = sp;
    return true;
}

bool Cpu::pop_value(unsigned width, uint64_t& out) {
    const unsigned sp_index = index_of(Reg::kRsp);
    const uint64_t sp = regs_[sp_index];
    if (!load(sp, width, out)) {
        return false;
    }
    regs_[sp_index] = sp + width;
    return true;
}

bool Cpu::load(uint64_t address, unsigned width, uint64_t& out) {
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

ExitStatus Cpu::execute_syscall() {
    const uint64_t nr = regs_[index_of(Reg::kRax)];
    switch (nr) {
    case 1: { // write(fd, buf, count): collect into the console sink.
        const uint64_t buf = regs_[index_of(Reg::kRsi)];
        const uint64_t count = regs_[index_of(Reg::kRdx)];
        for (uint64_t i = 0; i < count; ++i) {
            uint64_t byte = 0;
            if (!load(buf + i, 1, byte)) {
                return exit_;
            }
            console_.push_back(static_cast<char>(byte));
        }
        regs_[index_of(Reg::kRax)] = count; // bytes written
        retire(1);
        return exit_;
    }
    case 60:  // exit
    case 231: // exit_group
        latch(ExitStatus{
            .kind = ExitStatus::Kind::kExit,
            .code = regs_[index_of(Reg::kRdi)],
            .rip = insn_rip_,
            .detail = nr,
        });
        retire(1);
        return exit_;
    default:
        // Detail carries the syscall number: the batch-3 surface is
        // write/exit/exit_group, anything else names its caller precisely.
        latch(ExitStatus{
            .kind = ExitStatus::Kind::kUnsupported,
            .code = nr,
            .rip = insn_rip_,
            .detail = nr,
        });
        return exit_;
    }
}

ExitStatus Cpu::execute_string(const DecodedInsn& insn, uint64_t rip_after) {
    rip_ = rip_after;
    unsigned width = 0;
    enum class Family : uint8_t { kMovs, kStos, kLods, kScas, kCmps } family = Family::kMovs;
    switch (insn.mnemonic) {
    case Mnemonic::kMovsb:
        width = 1;
        family = Family::kMovs;
        break;
    case Mnemonic::kMovsw:
        width = 2;
        family = Family::kMovs;
        break;
    case Mnemonic::kMovsd:
        width = 4;
        family = Family::kMovs;
        break;
    case Mnemonic::kMovsq:
        width = 8;
        family = Family::kMovs;
        break;
    case Mnemonic::kStosb:
        width = 1;
        family = Family::kStos;
        break;
    case Mnemonic::kStosw:
        width = 2;
        family = Family::kStos;
        break;
    case Mnemonic::kStosd:
        width = 4;
        family = Family::kStos;
        break;
    case Mnemonic::kStosq:
        width = 8;
        family = Family::kStos;
        break;
    case Mnemonic::kLodsb:
        width = 1;
        family = Family::kLods;
        break;
    case Mnemonic::kLodsw:
        width = 2;
        family = Family::kLods;
        break;
    case Mnemonic::kLodsd:
        width = 4;
        family = Family::kLods;
        break;
    case Mnemonic::kLodsq:
        width = 8;
        family = Family::kLods;
        break;
    case Mnemonic::kScasb:
        width = 1;
        family = Family::kScas;
        break;
    case Mnemonic::kScasw:
        width = 2;
        family = Family::kScas;
        break;
    case Mnemonic::kScasd:
        width = 4;
        family = Family::kScas;
        break;
    case Mnemonic::kScasq:
        width = 8;
        family = Family::kScas;
        break;
    case Mnemonic::kCmpsb:
        width = 1;
        family = Family::kCmps;
        break;
    case Mnemonic::kCmpsw:
        width = 2;
        family = Family::kCmps;
        break;
    case Mnemonic::kCmpsd:
        width = 4;
        family = Family::kCmps;
        break;
    case Mnemonic::kCmpsq:
        width = 8;
        family = Family::kCmps;
        break;
    default:
        return unsupported(insn);
    }

    const unsigned si = index_of(Reg::kRsi);
    const unsigned di = index_of(Reg::kRdi);
    const unsigned cx = index_of(Reg::kRcx);
    const uint64_t step_width = df_ ? (std::numeric_limits<uint64_t>::max() - width + 1)
                                    : width; // ±width as a modular offset
    uint64_t remaining = (insn.rep == Rep::kNone) ? 1 : regs_[cx];

    while (remaining > 0) {
        uint64_t value = 0;
        switch (family) {
        case Family::kMovs:
            if (!load(regs_[si], width, value) || !store(regs_[di], width, value)) {
                return exit_;
            }
            regs_[si] += step_width;
            regs_[di] += step_width;
            break;
        case Family::kStos:
            value = read_gpr(Reg::kRax, width, true);
            if (!store(regs_[di], width, value)) {
                return exit_;
            }
            regs_[di] += step_width;
            break;
        case Family::kLods:
            if (!load(regs_[si], width, value)) {
                return exit_;
            }
            write_gpr(Reg::kRax, width, true, value);
            regs_[si] += step_width;
            break;
        case Family::kScas:
            if (!load(regs_[di], width, value)) {
                return exit_;
            }
            alu_sub(read_gpr(Reg::kRax, width, true), value, width, false);
            regs_[di] += step_width;
            break;
        case Family::kCmps:
            uint64_t lhs = 0;
            if (!load(regs_[si], width, lhs) || !load(regs_[di], width, value)) {
                return exit_;
            }
            alu_sub(lhs, value, width, false);
            regs_[si] += step_width;
            regs_[di] += step_width;
            break;
        }
        if (insn.rep != Rep::kNone) {
            regs_[cx] = regs_[cx] - 1;
        }
        retire(1);
        --remaining;
        // REPE/REPNE terminate scas/cmps on the flag they observe.
        if (insn.rep == Rep::kRep && (family == Family::kScas || family == Family::kCmps) && !zf_) {
            break;
        }
        if (insn.rep == Rep::kRepne && (family == Family::kScas || family == Family::kCmps) &&
            zf_) {
            break;
        }
    }
    return exit_;
}

} // namespace micro_forge::arch::x86_64
