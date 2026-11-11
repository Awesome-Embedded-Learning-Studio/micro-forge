/**
 * @file sse.cpp
 * @brief SSE/SSE2 subset execution (batch 6a): the memcpy path first.
 * @version 0.1.0
 *
 * The v0 subset follows the batch-2 profile: movaps/movups/movdqa/
 * movdqu, movq/movd transfers, the bitwise ops, padd/psub, the punpck
 * family, packuswb, the packed shifts, pcmpeq and pshufd. 128-bit guest
 * memory is composed from two 64-bit bus accesses (the bus has no
 * 16-byte width). Alignment is not checked: GUI/MMIO never reach these
 * ops, guest RAM is 64-byte aligned in practice, and the strict #GP on
 * misaligned movdqa waits for the fault model.
 */

#include "micro_forge/arch/x86_64/cpu.hpp"

#include "arch/x86_64/sse_ops.hpp"

#include "internal.hpp"
#include "micro_forge/kernel/le_access.hpp"

namespace micro_forge::arch::x86_64 {

using namespace sse_ops; // NOLINT(google-build-using-namespace)

ExitStatus Cpu::execute_sse(const DecodedInsn& insn, uint64_t rip_after) {
    const Operand& a = insn.ops[0];
    const Operand& b = insn.ops[1];

    const auto read_vec = [&](const Operand& op, Vec128& out) -> bool {
        if (op.kind == Operand::Kind::kReg) {
            out = xmm(op.reg);
            return true;
        }
        if (op.kind == Operand::Kind::kMem) {
            const uint64_t ea = effective_address(op, rip_after);
            uint64_t low = 0;
            uint64_t high = 0;
            if (!load(ea, 8, low) || !load(ea + 8U, 8, high)) {
                return false;
            }
            out = Vec128{low, high};
            return true;
        }
        return false;
    };
    const auto write_vec = [&](const Operand& op, const Vec128& value) -> bool {
        if (op.kind == Operand::Kind::kReg) {
            set_xmm(op.reg, value);
            return true;
        }
        if (op.kind == Operand::Kind::kMem) {
            const uint64_t ea = effective_address(op, rip_after);
            return store(ea, 8, value[0]) && store(ea + 8U, 8, value[1]);
        }
        return false;
    };

    switch (insn.mnemonic) {
    case Mnemonic::kMovaps:
    case Mnemonic::kMovups:
    case Mnemonic::kMovdqa:
    case Mnemonic::kMovdqu: {
        Vec128 v{};
        if (!read_vec(b, v) || !write_vec(a, v)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kMovq_xmm:
    case Mnemonic::kMovd_xmm: {
        // One XMM lane moves to/from r/m (either direction).
        const bool to_xmm = a.kind == Operand::Kind::kReg && static_cast<unsigned>(a.reg) >= 16U;
        const unsigned width = insn.mnemonic == Mnemonic::kMovd_xmm ? 4U : 8U;
        if (to_xmm) {
            uint64_t lane = 0;
            if (!read_operand(insn, b, rip_after, lane)) {
                return exit_;
            }
            if (!write_vec(a, Vec128{lane & width_util::width_mask(width), 0})) {
                return exit_;
            }
        } else {
            const Vec128 v = xmm(b.reg);
            if (!write_operand(insn, a, v[0] & width_util::width_mask(width))) {
                return exit_;
            }
        }
        break;
    }
    case Mnemonic::kPxor:
    case Mnemonic::kPand:
    case Mnemonic::kPandn:
    case Mnemonic::kPor: {
        Vec128 dst{};
        Vec128 src{};
        if (!read_vec(a, dst) || !read_vec(b, src)) {
            return exit_;
        }
        for (unsigned i = 0; i < 2; ++i) {
            if (insn.mnemonic == Mnemonic::kPxor) {
                dst[i] ^= src[i];
            } else if (insn.mnemonic == Mnemonic::kPand) {
                dst[i] &= src[i];
            } else if (insn.mnemonic == Mnemonic::kPandn) {
                dst[i] = ~dst[i] & src[i];
            } else {
                dst[i] |= src[i];
            }
        }
        if (!write_vec(a, dst)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kPaddd:
    case Mnemonic::kPaddq:
    case Mnemonic::kPsubd:
    case Mnemonic::kPaddb:
    case Mnemonic::kPsubb:
    case Mnemonic::kPaddw:
    case Mnemonic::kPsubw:
    case Mnemonic::kPsubq: {
        Vec128 dst{};
        Vec128 src{};
        if (!read_vec(a, dst) || !read_vec(b, src)) {
            return exit_;
        }
        sse_ops::pack_add_sub(insn.mnemonic, dst, src);
        if (!write_vec(a, dst)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kPminub:
    case Mnemonic::kPmaxub:
    case Mnemonic::kPminsw:
    case Mnemonic::kPmaxsw: {
        Vec128 dst{};
        Vec128 src{};
        if (!read_vec(a, dst) || !read_vec(b, src)) {
            return exit_;
        }
        sse_ops::pack_min_max(insn.mnemonic, dst, src);
        if (!write_vec(a, dst)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kPunpcklbw:
    case Mnemonic::kPunpcklwd:
    case Mnemonic::kPunpckldq:
    case Mnemonic::kPunpcklqdq:
    case Mnemonic::kPunpckhbw:
    case Mnemonic::kPunpckhwd:
    case Mnemonic::kPunpckhdq:
    case Mnemonic::kPunpckhqdq: {
        Vec128 dst{};
        Vec128 src{};
        if (!read_vec(a, dst) || !read_vec(b, src)) {
            return exit_;
        }
        unsigned elem_bytes = 1;
        const bool high =
            insn.mnemonic == Mnemonic::kPunpckhbw || insn.mnemonic == Mnemonic::kPunpckhwd ||
            insn.mnemonic == Mnemonic::kPunpckhdq || insn.mnemonic == Mnemonic::kPunpckhqdq;
        if (insn.mnemonic == Mnemonic::kPunpcklwd || insn.mnemonic == Mnemonic::kPunpckhwd) {
            elem_bytes = 2;
        } else if (insn.mnemonic == Mnemonic::kPunpckldq || insn.mnemonic == Mnemonic::kPunpckhdq) {
            elem_bytes = 4;
        } else if (insn.mnemonic == Mnemonic::kPunpcklqdq ||
                   insn.mnemonic == Mnemonic::kPunpckhqdq) {
            elem_bytes = 8;
        }
        const unsigned base_byte = high ? 8U : 0U;
        const unsigned count = 8U / elem_bytes;
        Vec128 out{};
        for (unsigned k = 0; k < count; ++k) {
            const unsigned lane_even = (k * 2U) * elem_bytes;
            const unsigned lane_odd = ((k * 2U) + 1U) * elem_bytes;
            for (unsigned e = 0; e < elem_bytes; ++e) {
                const unsigned src_byte = base_byte + (k * elem_bytes) + e;
                sse_ops::put_byte(out, lane_even + e, sse_ops::byte_at(dst, src_byte));
                sse_ops::put_byte(out, lane_odd + e, sse_ops::byte_at(src, src_byte));
            }
        }
        if (!write_vec(a, out)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kPackuswb: {
        Vec128 dst{};
        Vec128 src{};
        if (!read_vec(a, dst) || !read_vec(b, src)) {
            return exit_;
        }
        const auto saturate = [](uint64_t words) {
            const auto w = static_cast<int16_t>(words & 0xFFFFU);
            int32_t sat = w;
            if (sat < 0) {
                sat = 0;
            } else if (sat > 255) {
                sat = 255;
            }
            return static_cast<uint8_t>(sat);
        };
        Vec128 out{};
        for (unsigned i = 0; i < 8; ++i) {
            sse_ops::put_byte(out, i, saturate(dst[i / 4U] >> ((i % 4U) * 16U)));
            sse_ops::put_byte(out, 8U + i, saturate(src[i / 4U] >> ((i % 4U) * 16U)));
        }
        if (!write_vec(a, out)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kPsrldq:
    case Mnemonic::kPslldq: {
        // Byte shifts of the whole 128-bit register, zero-filled.
        Vec128 dst{};
        if (!read_vec(a, dst)) {
            return exit_;
        }
        uint64_t count = 0;
        if (b.kind == Operand::Kind::kImm) {
            count = static_cast<uint64_t>(b.disp);
        }
        if (count > 15U) {
            count = 16U;
        }
        const bool left = insn.mnemonic == Mnemonic::kPslldq;
        const auto shift_bytes = [&](unsigned byte) -> uint8_t {
            if (left) {
                const unsigned from = byte + static_cast<unsigned>(count);
                return from < 16U ? sse_ops::byte_at(dst, from) : 0;
            }
            const unsigned from = (byte >= count) ? static_cast<unsigned>(byte - count) : 0U;
            return byte >= count ? sse_ops::byte_at(dst, from) : 0;
        };
        Vec128 out{};
        for (unsigned byte = 0; byte < 16U; ++byte) {
            sse_ops::put_byte(out, byte,
                              left ? sse_ops::byte_at(dst, byte >= count ? 0U : byte) : 0);
        }
        (void)shift_bytes;
        // Recompute cleanly: dst shifted, zero fill.
        out = Vec128{};
        for (unsigned byte = 0; byte < 16U; ++byte) {
            if (left) {
                const unsigned src = (byte >= count) ? static_cast<unsigned>(byte - count) : 16U;
                sse_ops::put_byte(out, byte, src < 16U ? sse_ops::byte_at(dst, src) : 0);
            } else {
                const unsigned src = byte + static_cast<unsigned>(count);
                sse_ops::put_byte(out, byte, src < 16U ? sse_ops::byte_at(dst, src) : 0);
            }
        }
        if (!write_vec(a, out)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kPsrlw:
    case Mnemonic::kPsrld:
    case Mnemonic::kPsrlq:
    case Mnemonic::kPsllw:
    case Mnemonic::kPslld:
    case Mnemonic::kPsllq: {
        Vec128 dst{};
        if (!read_vec(a, dst)) {
            return exit_;
        }
        uint64_t count = 0;
        if (b.kind == Operand::Kind::kImm) {
            count = static_cast<uint64_t>(b.disp);
        } else if (b.kind == Operand::Kind::kReg) {
            count = xmm(b.reg)[0] & 0xFFULL;
        }
        unsigned elem_bytes = 2;
        bool left = false;
        switch (insn.mnemonic) {
        case Mnemonic::kPsrldq:
        case Mnemonic::kPslldq: {
            // Byte shifts of the whole 128-bit register, zero-filled.
            Vec128 dst{};
            if (!read_vec(a, dst)) {
                return exit_;
            }
            uint64_t count = 0;
            if (b.kind == Operand::Kind::kImm) {
                count = static_cast<uint64_t>(b.disp);
            }
            if (count > 15U) {
                count = 16U;
            }
            const bool left = insn.mnemonic == Mnemonic::kPslldq;
            const auto shift_bytes = [&](unsigned byte) -> uint8_t {
                if (left) {
                    const unsigned from = byte + static_cast<unsigned>(count);
                    return from < 16U ? sse_ops::byte_at(dst, from) : 0;
                }
                const unsigned from = (byte >= count) ? static_cast<unsigned>(byte - count) : 0U;
                return byte >= count ? sse_ops::byte_at(dst, from) : 0;
            };
            Vec128 out{};
            for (unsigned byte = 0; byte < 16U; ++byte) {
                sse_ops::put_byte(out, byte,
                                  left ? sse_ops::byte_at(dst, byte >= count ? 0U : byte) : 0);
            }
            (void)shift_bytes;
            // Recompute cleanly: dst shifted, zero fill.
            out = Vec128{};
            for (unsigned byte = 0; byte < 16U; ++byte) {
                if (left) {
                    const unsigned src =
                        (byte >= count) ? static_cast<unsigned>(byte - count) : 16U;
                    sse_ops::put_byte(out, byte, src < 16U ? sse_ops::byte_at(dst, src) : 0);
                } else {
                    const unsigned src = byte + static_cast<unsigned>(count);
                    sse_ops::put_byte(out, byte, src < 16U ? sse_ops::byte_at(dst, src) : 0);
                }
            }
            if (!write_vec(a, out)) {
                return exit_;
            }
            break;
        }
        case Mnemonic::kPsrlw:
            elem_bytes = 2;
            left = false;
            break;
        case Mnemonic::kPsrld:
            elem_bytes = 4;
            left = false;
            break;
        case Mnemonic::kPsrlq:
            elem_bytes = 8;
            left = false;
            break;
        case Mnemonic::kPsllw:
            elem_bytes = 2;
            left = true;
            break;
        case Mnemonic::kPslld:
            elem_bytes = 4;
            left = true;
            break;
        default:
            elem_bytes = 8;
            left = true;
            break;
        }
        const auto per = 8U / elem_bytes;
        const uint64_t mask = elem_bytes == 8 ? ~0ULL : ((1ULL << (8U * elem_bytes)) - 1ULL);
        for (unsigned half = 0; half < 2; ++half) {
            uint64_t word = 0;
            for (unsigned k = 0; k < per; ++k) {
                const unsigned shift = k * 8U * elem_bytes;
                const uint64_t elem = (dst[half] >> shift) & mask;
                const uint64_t moved = left ? ((elem << count) & mask) : (elem >> count);
                word |= (moved & mask) << shift;
            }
            dst[half] = word;
        }
        if (!write_vec(a, dst)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kPcmpeqb:
    case Mnemonic::kPcmpeqd:
    case Mnemonic::kPcmpeqq: {
        Vec128 dst{};
        Vec128 src{};
        if (!read_vec(a, dst) || !read_vec(b, src)) {
            return exit_;
        }
        unsigned elem_bytes = 1;
        if (insn.mnemonic == Mnemonic::kPcmpeqd) {
            elem_bytes = 4;
        } else if (insn.mnemonic == Mnemonic::kPcmpeqq) {
            elem_bytes = 8;
        }
        const auto per = 16U / elem_bytes;
        const uint64_t mask = elem_bytes == 8 ? ~0ULL : ((1ULL << (8U * elem_bytes)) - 1ULL);
        Vec128 out{};
        for (unsigned k = 0; k < per; ++k) {
            const unsigned bit = k * 8U * elem_bytes;
            const unsigned half = bit / 64U;
            const unsigned shift = bit % 64U;
            const uint64_t lhs = (dst[half] >> shift) & mask;
            const uint64_t rhs = (src[half] >> shift) & mask;
            out[half] |= (lhs == rhs ? mask : 0ULL) << shift;
        }
        if (!write_vec(a, out)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kMovlps:
    case Mnemonic::kMovlpd:
    case Mnemonic::kMovhps: {
        // Half-register moves. The store forms (modreg=0, XMM as source)
        // write 8 bytes; the load forms fill one lane and leave the other.
        const bool high = insn.mnemonic == Mnemonic::kMovhps;
        if (a.kind == Operand::Kind::kMem) {
            const Vec128 v = xmm(b.reg);
            if (!store(effective_address(a, rip_after), 8, high ? v[1] : v[0])) {
                return exit_;
            }
        } else {
            Vec128 v = xmm(a.reg);
            const uint64_t ea = effective_address(b, rip_after);
            uint64_t lane = 0;
            if (!load(ea, 8, lane)) {
                return exit_;
            }
            if (high) {
                v[1] = lane;
            } else {
                v[0] = lane;
            }
            if (!write_vec(a, v)) {
                return exit_;
            }
        }
        break;
    }
    case Mnemonic::kMovlhps:
    case Mnemonic::kMovhlps: {
        // Register halves: movlhps dst[1]=src[0], movhlps dst[0]=src[1].
        Vec128 dst{};
        Vec128 src{};
        if (!read_vec(a, dst) || !read_vec(b, src)) {
            return exit_;
        }
        if (insn.mnemonic == Mnemonic::kMovlhps) {
            dst[1] = src[0];
        } else {
            dst[0] = src[1];
        }
        if (!write_vec(a, dst)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kPmovmskb: {
        // Mask of the high bits of the 16 bytes, into a 32-bit GPR.
        if (b.kind != Operand::Kind::kReg) {
            return unsupported(insn);
        }
        const Vec128 src = xmm(b.reg);
        uint64_t mask = 0;
        for (unsigned i = 0; i < 16; ++i) {
            const auto bit =
                static_cast<uint64_t>((static_cast<unsigned>(sse_ops::byte_at(src, i)) >> 7U) & 1U);
            mask |= bit << i;
        }
        if (!write_operand(insn, a, mask)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kShufps: {
        // dst[0..1] picked from dst, dst[2..3] from src, imm8 selects
        // each dword with its two-bit field.
        Vec128 dst{};
        Vec128 src{};
        if (!read_vec(a, dst) || !read_vec(b, src)) {
            return exit_;
        }
        uint64_t ctrl = 0;
        if (insn.ops[2].kind == Operand::Kind::kImm) {
            ctrl = static_cast<uint64_t>(insn.ops[2].disp);
        }
        Vec128 out{};
        for (unsigned i = 0; i < 4; ++i) {
            const auto pick = static_cast<unsigned>((ctrl >> (2U * i)) & 3U);
            const uint32_t value = sse_ops::elem32(i < 2 ? dst : src, pick);
            out[i / 2U] |= static_cast<uint64_t>(value) << ((i % 2U) * 32U);
        }
        if (!write_vec(a, out)) {
            return exit_;
        }
        break;
    }
    case Mnemonic::kFxsave:
    case Mnemonic::kFxsave64: {
        // 512-byte image: MXCSR at 24, XMM0-15 at 160. The x87 area is
        // zeroed (the engine executes no x87 instructions; tasks see
        // consistent SSE state across switches).
        std::array<uint8_t, 512> image{};
        kernel::put_le32(image.data() + 24, mxcsr_);
        kernel::put_le32(image.data() + 28, 0xFFFFU); // MXCSR_MASK: all bits
        for (unsigned i = 0; i < 16; ++i) {
            for (unsigned half = 0; half < 2; ++half) {
                const uint64_t word = xmm_[i][half];
                for (unsigned j = 0; j < 8; ++j) {
                    image[160 + (i * 16U) + (half * 8U) + j] =
                        static_cast<uint8_t>(word >> (8U * j));
                }
            }
        }
        const uint64_t ea = effective_address(a, rip_after);
        for (unsigned i = 0; i < 512; i += 8) {
            uint64_t word = 0;
            for (unsigned j = 0; j < 8; ++j) {
                word |= static_cast<uint64_t>(image[i + j]) << (8U * j);
            }
            if (!store(ea + i, 8, word)) {
                return exit_;
            }
        }
        break;
    }
    case Mnemonic::kFxrstor:
    case Mnemonic::kFxrstor64: {
        const uint64_t ea = effective_address(a, rip_after);
        std::array<uint8_t, 512> image{};
        for (unsigned i = 0; i < 512; i += 8) {
            uint64_t word = 0;
            if (!load(ea + i, 8, word)) {
                return exit_;
            }
            for (unsigned j = 0; j < 8; ++j) {
                image[i + j] = static_cast<uint8_t>(word >> (8U * j));
            }
        }
        mxcsr_ = kernel::get_le32(image.data() + 24);
        for (unsigned i = 0; i < 16; ++i) {
            for (unsigned half = 0; half < 2; ++half) {
                uint64_t word = 0;
                for (unsigned j = 0; j < 8; ++j) {
                    word |= static_cast<uint64_t>(image[160 + (i * 16U) + (half * 8U) + j])
                            << (8U * j);
                }
                xmm_[i][half] = word;
            }
        }
        break;
    }
    case Mnemonic::kPshufd: {
        Vec128 src{};
        if (!read_vec(b, src)) {
            return exit_;
        }
        uint64_t ctrl = 0;
        if (insn.ops[2].kind == Operand::Kind::kImm) {
            ctrl = static_cast<uint64_t>(insn.ops[2].disp);
        }
        Vec128 out{};
        for (unsigned i = 0; i < 4; ++i) {
            const auto pick = static_cast<unsigned>((ctrl >> (2U * i)) & 3U);
            out[i / 2U] |= static_cast<uint64_t>(sse_ops::elem32(src, pick)) << ((i % 2U) * 32U);
        }
        if (!write_vec(a, out)) {
            return exit_;
        }
        break;
    }
    default:
        return execute_sse_ext(insn, rip_after); // 2026-11 face: sse_ext.cpp
    }
    return exit_; // kRunning: the caller retires the instruction
}

} // namespace micro_forge::arch::x86_64
