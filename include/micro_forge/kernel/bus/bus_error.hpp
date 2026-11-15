/**
 * @file bus_error.hpp
 * @brief Error channel shared by the bus and all regions.
 * @version 0.1.0
 */

#pragma once

#include <cstdint>

namespace micro_forge::kernel {

/**
 * @brief Error type for the whole bus/region channel.
 *
 * Address semantics: errors that escape to the caller carry absolute
 * guest addresses. The bus rewrites region-local offsets before
 * propagating.
 */
struct BusError {
    /// @brief Why the access (or mapping attempt) failed.
    enum class Kind : uint8_t {
        kUnmappedRead,   ///< No region covers the address.
        kUnmappedWrite,  ///< No region covers the address.
        kOutOfBounds,    ///< Access crosses the owning region's boundary.
        kOverlap,        ///< map(): range overlaps an existing mapping.
        kInvalidMapping, ///< map(): zero size or address wrap-around.
    };
    Kind kind;
    /// Faulting guest address; for map() errors, the offending mapping base.
    uint64_t address;
};

} // namespace micro_forge::kernel
