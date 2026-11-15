/**
 * @file cpu.cpp
 * @brief CPU core loop: fetch/decode/step, syscall surface, string ops.
 * @version 0.1.0
 */

#include "micro_forge/arch/x86_64/cpu.hpp"

#include "micro_forge/arch/x86_64/cached_insn.hpp"
#include "micro_forge/arch/x86_64/decoded.hpp"
#include "micro_forge/arch/x86_64/decoder.hpp"
#include "micro_forge/kernel/bit.hpp"
#include "micro_forge/kernel/bus/bus.hpp"
#include "micro_forge/arch/x86_64/port_space.hpp"
#include "micro_forge/kernel/scheduler.hpp"
#include "micro_forge/kernel/snapshot.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace micro_forge::arch::x86_64 {

Cpu::Cpu(kernel::Bus& bus, kernel::Scheduler& scheduler, arch::x86_64::PortSpace* ports)
    : bus_(bus), scheduler_(scheduler), ports_(ports) {}

void Cpu::set_trace(std::function<void(uint64_t rip)> trace) {
    trace_ = std::move(trace);
}

void Cpu::request_interrupt(uint8_t vector) {
    pending_vector_ = vector;
    interrupt_pending_ = true;
}

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
    const auto rip = reader.u64();
    if (!rip) {
        return false;
    }
    rip_ = *rip;
    for (uint64_t& r : regs_) {
        const auto v = reader.u64();
        if (!v) {
            return false;
        }
        r = *v;
    }
    const auto flags = reader.u8();
    const auto flags2 = reader.u8();
    if (!flags || !flags2) {
        return false;
    }
    if_ = (*flags2 & kernel::bit::mask_bit(0)) != 0;
    ac_ = (*flags2 & kernel::bit::mask_bit(1)) != 0;
    cf_ = kernel::bit::extract(*flags, {
                                          .low = 0,
                                          .width = 1,
                                      }) != 0;
    pf_ = kernel::bit::extract(*flags, {
                                          .low = 1,
                                          .width = 1,
                                      }) != 0;
    af_ = kernel::bit::extract(*flags, {
                                          .low = 2,
                                          .width = 1,
                                      }) != 0;
    zf_ = kernel::bit::extract(*flags, {
                                          .low = 3,
                                          .width = 1,
                                      }) != 0;
    sf_ = kernel::bit::extract(*flags, {
                                          .low = 4,
                                          .width = 1,
                                      }) != 0;
    of_ = kernel::bit::extract(*flags, {
                                          .low = 5,
                                          .width = 1,
                                      }) != 0;
    df_ = kernel::bit::extract(*flags, {
                                          .low = 6,
                                          .width = 1,
                                      }) != 0;
    const auto icount = reader.u64();
    const auto console = reader.bytes();
    if (!icount || !console) {
        return false;
    }
    icount_ = *icount;
    console_.assign(console->begin(), console->end());
    insn_rip_ = rip_; // next step() refreshes both instruction-local fields
    cur_length_ = 0;
    exit_ = ExitStatus{};
    return true;
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
    retire_deferred(n);
    retire_poll();
}

void Cpu::retire_deferred(unsigned n) {
    icount_ += n;
}

void Cpu::retire_poll() {
    scheduler_.dispatch(icount_);
    if (irq_line_ != nullptr && irq_line_->pending() && irq_hook_) {
        irq_hook_(); // device acknowledgement / EOI hook
    }
    if (interrupt_pending_) {
        // STI shadow: after STI opens IF, exactly one more instruction
        // retires before the queued vector is recognized (SDM STI).
        if (sti_shadow_) {
            sti_shadow_ = false;
        } else {
            deliver_interrupt();
        }
    }
    if (trace_) {
        trace_(insn_rip_);
    }
    // Chain deadline: instructions the block chain may retire before the
    // next device event (or a queued interrupt) must surface. Blocks retire
    // per instruction on the interpreter path, so 1 keeps that granularity.
    const auto next = scheduler_.next_deadline();
    const bool wake = interrupt_pending_ || (irq_line_ != nullptr && irq_line_->pending());
    const auto due = next.has_value() && *next > icount_ ? *next - icount_ : 1;
    if (wake) {
        jit_chain_[2] = 1; // interrupt wants the surface at the next check
    } else {
        jit_chain_[2] = next.has_value() ? due : UINT64_MAX;
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
    switch_address_space();
}

void Cpu::switch_address_space() {
    decode_cache_.clear();
    std::ranges::fill(code_page_bits_, 0ULL);
    ++code_epoch_;
    ++tlb_epoch_; // every translation derived from the old CR3 is stale
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
    const CachedInsn* const cached = decode_cache_.find(rip_);
    if (cached != nullptr && cached->cr3 == cr_[3]) {
        // Insertion already marked the code page. note_store() clears
        // both the bitmap and the cache together, so a live hit cannot
        // need to mark it again.
        insn_rip_ = rip_;
        cur_length_ = cached->insn.length;
        const uint64_t rip_after = rip_ + cached->insn.length;
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
    CachedInsn entry = cache_insn(*decoded);
    entry.cr3 = cr_[3];
    decode_cache_.put(rip_, entry);
    maybe_compile_block(rip_);
    return execute_insn(*decoded, rip_ + decoded->length);
}

ExitStatus Cpu::run(uint64_t max_instructions) {
    const kernel::Tick start = icount_;
    jit_run_end_ = start + max_instructions; // block-chain budget guard
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

void Cpu::attach_irq(kernel::IrqLine& line, std::function<void()> on_pending) {
    irq_line_ = &line;
    irq_hook_ = std::move(on_pending);
}

std::span<const uint8_t> Cpu::last_fault_bytes() const {
    return {last_fault_bytes_.data(), last_fault_bytes_.size()};
}

void Cpu::set_code_range(uint64_t lo, uint64_t hi) {
    code_lo_ = lo;
    code_hi_ = hi;
}

void Cpu::flush_tlb() {
    mmu_.flush();
    ++tlb_epoch_;
}

void Cpu::invalidate_page(uint64_t vaddr) {
    mmu_.invalidate(vaddr);
    ++tlb_epoch_; // block-side TLB clears on the next block run
    if (decode_cache_.erase_if([vaddr](uint64_t rip) {
            return (rip >> 12U) == (vaddr >> 12U); // same vaddr page
        }) != 0U) {
        ++code_epoch_; // JIT mirrors revalidate lazily
    }
}

void Cpu::set_gdtr(uint64_t base, uint16_t limit) {
    gdtr_base_ = base;
    gdtr_limit_ = limit;
}

} // namespace micro_forge::arch::x86_64
