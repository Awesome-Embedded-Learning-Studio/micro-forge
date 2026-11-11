/**
 * @file cpu.cpp
 * @brief CPU core loop: fetch/decode/step, syscall surface, string ops.
 * @version 0.1.0
 */

#include "micro_forge/arch/x86_64/cpu.hpp"

#include <cstdlib>

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
    if_ = false;
    ac_ = false;
    // Long-mode entry state the boot shim hands over: paging and
    // protection enabled, no EFER bits, empty tables.
    // protection enabled, no EFER bits, empty tables.
    cr_.fill(0);
    cr_[0] = 0x80000001ULL; // PE | PG
    dr_.fill(0);
    gdtr_base_ = 0;
    gdtr_limit_ = 0;
    idtr_base_ = 0;
    idtr_limit_ = 0;
    msr_.clear();
    msr_[0xC0000080U] = 0; // EFER
    efer_value_ = 0;
    msr_[0x0000001BU] = 0xFEE00900ULL; // APIC_BASE: 0xFEE00000, EN | BSP
    fs_base_ = 0;
    gs_base_ = 0;
    kernel_gs_base_ = 0;
    icount_ = 0;
    console_.clear();
    exit_ = ExitStatus{};
}

void Cpu::save(kernel::SnapshotWriter& writer) const {
    writer.u64(rip_);
    for (const uint64_t r : regs_) {
        writer.u64(r);
    }
    uint64_t flags = 0;
    if (cf_) {
        flags |= kernel::bit::mask_bit(0);
    }
    if (pf_) {
        flags |= kernel::bit::mask_bit(1);
    }
    if (af_) {
        flags |= kernel::bit::mask_bit(2);
    }
    if (zf_) {
        flags |= kernel::bit::mask_bit(3);
    }
    if (sf_) {
        flags |= kernel::bit::mask_bit(4);
    }
    if (of_) {
        flags |= kernel::bit::mask_bit(5);
    }
    if (df_) {
        flags |= kernel::bit::mask_bit(6);
    }
    writer.u8(static_cast<uint8_t>(flags));
    uint8_t flags2 = 0;
    if (if_) {
        flags2 |= kernel::bit::mask_bit(0);
    }
    if (ac_) {
        flags2 |= kernel::bit::mask_bit(1);
    }
    writer.u8(flags2);
    writer.u64(icount_);
    writer.bytes({reinterpret_cast<const uint8_t*>(console_.data()), console_.size()});
}
bool Cpu::restore(kernel::SnapshotReader& reader) {
    bool ok = true;
    rip_ = reader.u64(ok);
    for (uint64_t& r : regs_) {
        r = reader.u64(ok);
    }
    const uint64_t flags = reader.u8(ok);
    const uint64_t flags2 = reader.u8(ok);
    if_ = (flags2 & kernel::bit::mask_bit(0)) != 0;
    ac_ = (flags2 & kernel::bit::mask_bit(1)) != 0;
    cf_ = kernel::bit::extract(flags, {
                                          .low = 0,
                                          .width = 1,
                                      }) != 0;
    pf_ = kernel::bit::extract(flags, {
                                          .low = 1,
                                          .width = 1,
                                      }) != 0;
    af_ = kernel::bit::extract(flags, {
                                          .low = 2,
                                          .width = 1,
                                      }) != 0;
    zf_ = kernel::bit::extract(flags, {
                                          .low = 3,
                                          .width = 1,
                                      }) != 0;
    sf_ = kernel::bit::extract(flags, {
                                          .low = 4,
                                          .width = 1,
                                      }) != 0;
    of_ = kernel::bit::extract(flags, {
                                          .low = 5,
                                          .width = 1,
                                      }) != 0;
    df_ = kernel::bit::extract(flags, {
                                          .low = 6,
                                          .width = 1,
                                      }) != 0;
    icount_ = reader.u64(ok);
    const std::vector<uint8_t> console = reader.bytes(ok);
    console_.assign(console.begin(), console.end());
    insn_rip_ = rip_; // next step() refreshes both instruction-local fields
    cur_length_ = 0;
    exit_ = ExitStatus{};
    return ok;
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
    // Batched: a JIT block retires its whole run at once. Scheduler events
    // can slide to the end of the block (<= 64 instructions), which is far
    // below the 100 Hz device tick.
    icount_ += n;
    scheduler_.dispatch(icount_);
    if (irq_line_ != nullptr && irq_line_->pending() && irq_hook_) {
        irq_hook_(); // device acknowledgement / EOI hook
    }
    if (interrupt_pending_) {
        deliver_interrupt();
    }
    if (trace_) {
        trace_(insn_rip_);
    }
}

bool Cpu::fetch(unsigned& got) {
    got = 0;
    // Fast path: translate the start once; when the whole fetch window sits
    // inside the same page and the mapping offers a contiguous view (RAM),
    // copy the block in one go. Profiling showed the per-byte translate +
    // bus lookup dominating the run loop.
    const auto first = translate_access(rip_, false, true, rip_);
    if (!first.ok) {
        return false; // #PF (when injected) is consumed by step()
    }
    const uint64_t offset = rip_ & 0xFFFULL;
    const uint64_t room = 0x1000ULL - offset;
    if (room >= fetch_buf_.size()) {
        const auto block = bus_.peek(first.paddr, fetch_buf_.size());
        if (block.size() == fetch_buf_.size()) {
            for (std::size_t i = 0; i < fetch_buf_.size(); ++i) {
                fetch_buf_[i] = block[i];
            }
            got = static_cast<unsigned>(fetch_buf_.size());
            return true;
        }
    }
    // Slow path: byte by byte (cross-page fetch, or a device mapping).
    while (got < fetch_buf_.size()) {
        // Frame RIP is the instruction start, never the byte offset inside it.
        const auto translated = got == 0 ? first : translate_access(rip_ + got, false, true, rip_);
        if (!translated.ok) {
            return false; // #PF (when injected) is consumed by step()
        }
        const auto byte = bus_.read_u8(translated.paddr);
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

void Cpu::configure_caches(uint64_t ram_size, std::size_t decode_capacity) {
    // Exact coverage: one bit per physical page of this machine.
    const auto pages = static_cast<std::size_t>(ram_size >> 12U);
    code_page_bits_.assign((pages + 63U) / 64U, 0);
    decode_cache_ = DecodeCache(decode_capacity);
}

void Cpu::mark_code_page(uint64_t paddr) {
    const auto page = static_cast<std::size_t>(paddr >> 12U);
    const std::size_t index = page / 64U;
    if (index >= code_page_bits_.size()) {
        return; // outside guest RAM (should not happen for fetches)
    }
    code_page_bits_[index] |= (1ULL << (page % 64U));
}

void Cpu::note_store(uint64_t paddr) {
    const auto page = static_cast<std::size_t>(paddr >> 12U);
    const std::size_t index = page / 64U;
    if (index >= code_page_bits_.size()) {
        return;
    }
    if ((code_page_bits_[index] & (1ULL << (page % 64U))) == 0) {
        return;
    }
    // The store may have rewritten executable bytes: drop every cached
    // decode and forget the page set (conservative but cheap; stores to
    // code pages are rare after the loader finishes).
    decode_cache_.clear();
    std::ranges::fill(code_page_bits_, 0ULL);
}

ExitStatus Cpu::step() {
    if (exit_.kind != ExitStatus::Kind::kRunning) {
        return exit_;
    }
    if (run_jit_block()) {
        return exit_;
    }
    // Decode cache: the hot path repeats the same instructions; a hit
    // skips fetch + decode entirely. The hit path mirrors the slow path
    // exactly (insn_rip_, cur_length_, dispatch by mnemonic).
    if (const CachedInsn* cached = decode_cache_.find(rip_)) {
        const DecodedInsn& insn = cached->insn;
        insn_rip_ = rip_;
        cur_length_ = insn.length;
        const uint64_t rip_after = rip_ + insn.length;
        // Insertion already marked the code page. note_store() clears
        // both the bitmap and the cache together, so a live hit cannot
        // need to mark it again.
        rip_ = rip_after;
        return execute_cached(*cached, rip_after);
    }
    unsigned got = 0;
    if (!fetch(got)) {
        if (exception_injected_) {
            exception_injected_ = false;
            return exit_; // #PF delivered; resume at the handler
        }
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
    {
        const auto first = translate_access(rip_, false, true, rip_);
        if (first.ok) {
            mark_code_page(first.paddr);
        }
    }
    // Repeat visitors skip fetch+decode (and the translation below).
    decode_cache_.put(rip_, cache_insn(*decoded));
    maybe_compile_block(rip_);
    return execute_insn(*decoded, rip_ + decoded->length);
}

ExitStatus Cpu::run(uint64_t max_instructions) {
    const kernel::Tick start = icount_;
    while (exit_.kind == ExitStatus::Kind::kRunning && icount_ - start < max_instructions) {
        (void)step();
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

ExitStatus Cpu::execute_string(const DecodedInsn& insn, uint64_t rip_after) {
    rip_ = rip_after;
    if (insn.seg == Seg::kFs || insn.seg == Seg::kGs) {
        // String ops bypass read_operand, so the fs/gs refusal lives here
        // too (batch-4 review: this path used to address with base 0).
        return unsupported(insn);
    }
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
