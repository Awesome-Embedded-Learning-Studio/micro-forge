/// @file jit_helpers.cpp
/// @brief Maps a JitHelper id to the C++ thunk address the template calls.
///
/// Kept out of jit_cpu.cpp because the one-case-per-helper switch is long
/// and the compile loop has a file-line budget to respect.
#include "jit_thunks.hpp"

namespace micro_forge::arch::x86_64 {

/// @brief Returns the thunk address for @p id, or 0 when nothing matches.
std::uint64_t mf_jit_helper_address(micro_forge::base::JitHelper id) {
    switch (id) {
    case micro_forge::base::JitHelper::kAdd:
        return reinterpret_cast<std::uint64_t>(&mf_jit_add);
    case micro_forge::base::JitHelper::kAddMem:
        return reinterpret_cast<std::uint64_t>(&mf_jit_add_mem);
    case micro_forge::base::JitHelper::kStore:
        return reinterpret_cast<std::uint64_t>(&mf_jit_store);
    case micro_forge::base::JitHelper::kLoad:
        return reinterpret_cast<std::uint64_t>(&mf_jit_load);
    case micro_forge::base::JitHelper::kCmp:
        return reinterpret_cast<std::uint64_t>(&mf_jit_cmp);
    case micro_forge::base::JitHelper::kTest:
        return reinterpret_cast<std::uint64_t>(&mf_jit_test);
    case micro_forge::base::JitHelper::kSub:
        return reinterpret_cast<std::uint64_t>(&mf_jit_sub);
    case micro_forge::base::JitHelper::kAnd:
        return reinterpret_cast<std::uint64_t>(&mf_jit_and);
    case micro_forge::base::JitHelper::kOr:
        return reinterpret_cast<std::uint64_t>(&mf_jit_or);
    case micro_forge::base::JitHelper::kXor:
        return reinterpret_cast<std::uint64_t>(&mf_jit_xor);
    case micro_forge::base::JitHelper::kImul:
        return reinterpret_cast<std::uint64_t>(&mf_jit_imul);
    case micro_forge::base::JitHelper::kCmpMem:
        return reinterpret_cast<std::uint64_t>(&mf_jit_cmp_mem);
    case micro_forge::base::JitHelper::kCmov:
        return reinterpret_cast<std::uint64_t>(&mf_jit_cmov);
    case micro_forge::base::JitHelper::kSetcc:
        return reinterpret_cast<std::uint64_t>(&mf_jit_setcc);
    case micro_forge::base::JitHelper::kPushVal:
        return reinterpret_cast<std::uint64_t>(&mf_jit_push_val);
    case micro_forge::base::JitHelper::kPushImm:
        return reinterpret_cast<std::uint64_t>(&mf_jit_push_imm);
    case micro_forge::base::JitHelper::kPopReg:
        return reinterpret_cast<std::uint64_t>(&mf_jit_pop_reg);
    case micro_forge::base::JitHelper::kLoadXmm:
        return reinterpret_cast<std::uint64_t>(&mf_jit_load_xmm);
    case micro_forge::base::JitHelper::kStoreXmm:
        return reinterpret_cast<std::uint64_t>(&mf_jit_store_xmm);
    case micro_forge::base::JitHelper::kRet:
        return reinterpret_cast<std::uint64_t>(&mf_jit_ret);
    case micro_forge::base::JitHelper::kLeave:
        return reinterpret_cast<std::uint64_t>(&mf_jit_leave);
    case micro_forge::base::JitHelper::kCall:
        return reinterpret_cast<std::uint64_t>(&mf_jit_call);
    case micro_forge::base::JitHelper::kJmp:
        return reinterpret_cast<std::uint64_t>(&mf_jit_jmp);
    case micro_forge::base::JitHelper::kJcc:
        return reinterpret_cast<std::uint64_t>(&mf_jit_jcc);
    case micro_forge::base::JitHelper::kMovSlotW:
        return reinterpret_cast<std::uint64_t>(&mf_jit_mov_slot_w);
    case micro_forge::base::JitHelper::kShift:
        return reinterpret_cast<std::uint64_t>(&mf_jit_shift);
    case micro_forge::base::JitHelper::kAluReg:
        return reinterpret_cast<std::uint64_t>(&mf_jit_alu_reg);
    case micro_forge::base::JitHelper::kAluMem:
        return reinterpret_cast<std::uint64_t>(&mf_jit_alu_mem);
    case micro_forge::base::JitHelper::kShiftCl:
        return reinterpret_cast<std::uint64_t>(&mf_jit_shift);
    case micro_forge::base::JitHelper::kPxor:
        return reinterpret_cast<std::uint64_t>(&mf_jit_pxor);
    case micro_forge::base::JitHelper::kAluMemRip:
        return reinterpret_cast<std::uint64_t>(&mf_jit_alu_mem);
    case micro_forge::base::JitHelper::kLoadRip:
        return reinterpret_cast<std::uint64_t>(&mf_jit_load);
    case micro_forge::base::JitHelper::kGrp3:
        return reinterpret_cast<std::uint64_t>(&mf_jit_grp3);
    case micro_forge::base::JitHelper::kImul23:
        return reinterpret_cast<std::uint64_t>(&mf_jit_imul23);
    case micro_forge::base::JitHelper::kBt:
        return reinterpret_cast<std::uint64_t>(&mf_jit_bt);
    case micro_forge::base::JitHelper::kCmovMem:
        return reinterpret_cast<std::uint64_t>(&mf_jit_cmov_mem);
    case micro_forge::base::JitHelper::kNot:
        return reinterpret_cast<std::uint64_t>(&mf_jit_not);
    case micro_forge::base::JitHelper::kAluMemImm:
        return reinterpret_cast<std::uint64_t>(&mf_jit_alu_mem_imm);
    case micro_forge::base::JitHelper::kImulMem:
        return reinterpret_cast<std::uint64_t>(&mf_jit_imul_mem);
    case micro_forge::base::JitHelper::kPorts:
        return reinterpret_cast<std::uint64_t>(&mf_jit_ports);
    case micro_forge::base::JitHelper::kAluMemIdx:
        return reinterpret_cast<std::uint64_t>(&mf_jit_alu_mem_idx);
    case micro_forge::base::JitHelper::kGrp3Mem:
        return reinterpret_cast<std::uint64_t>(&mf_jit_grp3_mem);
    case micro_forge::base::JitHelper::kMovsx:
        return reinterpret_cast<std::uint64_t>(&mf_jit_movsx);
    case micro_forge::base::JitHelper::kNeg:
        return reinterpret_cast<std::uint64_t>(&mf_jit_neg);
    case micro_forge::base::JitHelper::kBts:
        return reinterpret_cast<std::uint64_t>(&mf_jit_bts);
    case micro_forge::base::JitHelper::kBtr:
        return reinterpret_cast<std::uint64_t>(&mf_jit_btr);
    case micro_forge::base::JitHelper::kBtc:
        return reinterpret_cast<std::uint64_t>(&mf_jit_btc);
    default:
        return 0;
    }
}

} // namespace micro_forge::arch::x86_64
