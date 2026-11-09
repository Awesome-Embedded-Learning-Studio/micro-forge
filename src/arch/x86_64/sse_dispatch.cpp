/**
 * @file sse_dispatch.cpp
 * @brief The SSE mnemonic predicate (keeps dispatch.cpp small).
 * @version 0.1.0
 */

#include "micro_forge/arch/x86_64/cpu.hpp"

namespace micro_forge::arch::x86_64 {

bool Cpu::is_sse_mnemonic(Mnemonic mnemonic) {
    switch (mnemonic) {
    case Mnemonic::kMovaps:
    case Mnemonic::kMovups:
    case Mnemonic::kMovdqa:
    case Mnemonic::kMovdqu:
    case Mnemonic::kMovq_xmm:
    case Mnemonic::kMovd_xmm:
    case Mnemonic::kPxor:
    case Mnemonic::kPand:
    case Mnemonic::kPandn:
    case Mnemonic::kPor:
    case Mnemonic::kPaddd:
    case Mnemonic::kPaddq:
    case Mnemonic::kPsubd:
    case Mnemonic::kPaddb:
    case Mnemonic::kPsubb:
    case Mnemonic::kPaddw:
    case Mnemonic::kPsubw:
    case Mnemonic::kPsubq:
    case Mnemonic::kPminub:
    case Mnemonic::kPmaxub:
    case Mnemonic::kPminsw:
    case Mnemonic::kPmaxsw:
    case Mnemonic::kPunpcklbw:
    case Mnemonic::kPunpcklwd:
    case Mnemonic::kPunpckldq:
    case Mnemonic::kPunpcklqdq:
    case Mnemonic::kPunpckhbw:
    case Mnemonic::kPunpckhwd:
    case Mnemonic::kPunpckhdq:
    case Mnemonic::kPunpckhqdq:
    case Mnemonic::kPackuswb:
    case Mnemonic::kPsrlw:
    case Mnemonic::kPsrld:
    case Mnemonic::kPsrlq:
    case Mnemonic::kPsllw:
    case Mnemonic::kPslld:
    case Mnemonic::kPsllq:
    case Mnemonic::kPcmpeqb:
    case Mnemonic::kPcmpeqd:
    case Mnemonic::kPcmpeqq:
    case Mnemonic::kPshufd:
    case Mnemonic::kMovlps:
    case Mnemonic::kMovhps:
    case Mnemonic::kMovlhps:
    case Mnemonic::kMovhlps:
    case Mnemonic::kPmovmskb:
    case Mnemonic::kPsrldq:
    case Mnemonic::kPslldq:
    case Mnemonic::kShufps:
    case Mnemonic::kFxsave:
    case Mnemonic::kFxsave64:
    case Mnemonic::kFxrstor:
    case Mnemonic::kFxrstor64:
        return true;
    default:
        return false;
    }
}

} // namespace micro_forge::arch::x86_64
