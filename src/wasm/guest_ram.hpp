/**
 * @file guest_ram.hpp
 * @brief The wasm artifact's default guest RAM size constant.
 * @version 0.1.0
 */

#pragma once

#include <cstdint>

// The wasm recipe always defines GUEST_RAM_MB explicitly. The default
// matches exports.cpp's original fallback so a plain build still gets
// 512 MiB.
#ifndef GUEST_RAM_MB
#    define GUEST_RAM_MB 512
#endif

namespace micro_forge::wasm {

/// Default guest RAM size in bytes (GUEST_RAM_MB << 20).
inline constexpr uint64_t kGuestRam = static_cast<uint64_t>(GUEST_RAM_MB) << 20U;

} // namespace micro_forge::wasm
