/**
 * @file run_result.hpp
 * @brief Architecture-independent result of a bounded machine run.
 * @version 0.1.0
 */

#pragma once

#include <cstdint>

namespace micro_forge {

/** @brief Why a bounded run returned to its caller. */
enum class StopReason : uint8_t {
    kBudgetExhausted, ///< The machine can continue with another budget.
    kHalted,          ///< Execution stopped at a halt instruction.
    kExited,          ///< The guest requested a normal exit.
    kDebugExit,       ///< A test exit device stopped execution.
    kDecodeError,     ///< Instruction decoding failed.
    kUnsupported,     ///< A decoded operation is not implemented.
    kFault,           ///< Execution stopped on a guest fault.
};

/**
 * @brief Common stop information without an architecture-specific register set.
 *
 * Budget exhaustion is resumable. Halt and fault interpretation belongs to
 * the machine: a guest exception handled internally is not a stop here.
 * Code and detail preserve machine-specific diagnostics; their enumeration
 * values are not a cross-machine protocol or the wasm ABI.
 */
struct RunResult {
    StopReason reason = StopReason::kBudgetExhausted; ///< Why control returned.
    uint64_t pc = 0;                                  ///< Guest instruction address.
    uint64_t code = 0;                                ///< Exit code or fault address.
    uint64_t detail = 0;                              ///< Machine-specific diagnostic.
};

} // namespace micro_forge
