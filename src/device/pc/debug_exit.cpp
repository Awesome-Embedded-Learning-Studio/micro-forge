/**
 * @file debug_exit.cpp
 * @brief Debug-exit region bodies (debug_exit.hpp).
 * @version 0.1.0
 */

#include "micro_forge/device/pc/debug_exit.hpp"

namespace micro_forge::device::pc {

std::expected<uint8_t, kernel::BusError> DebugExitRegion::read_u8(uint64_t offset) {
    return unexpected_out_of_bounds(offset);
}

std::expected<uint16_t, kernel::BusError> DebugExitRegion::read_u16(uint64_t offset) {
    return unexpected_out_of_bounds(offset);
}

std::expected<uint32_t, kernel::BusError> DebugExitRegion::read_u32(uint64_t offset) {
    return unexpected_out_of_bounds(offset);
}

std::expected<uint64_t, kernel::BusError> DebugExitRegion::read_u64(uint64_t offset) {
    return unexpected_out_of_bounds(offset);
}

std::expected<void, kernel::BusError> DebugExitRegion::write_u8(uint64_t offset,
                                                                uint8_t /*value*/) {
    return unexpected_out_of_bounds(offset);
}

std::expected<void, kernel::BusError> DebugExitRegion::write_u16(uint64_t offset,
                                                                 uint16_t /*value*/) {
    return unexpected_out_of_bounds(offset);
}

std::expected<void, kernel::BusError> DebugExitRegion::write_u32(uint64_t offset, uint32_t value) {
    if (offset == kRegExit) {
        on_exit_((value << 1U) | 1U);
        return {};
    }
    return unexpected_out_of_bounds(offset);
}

std::expected<void, kernel::BusError> DebugExitRegion::write_u64(uint64_t offset,
                                                                 uint64_t /*value*/) {
    return unexpected_out_of_bounds(offset);
}

std::unexpected<kernel::BusError> DebugExitRegion::unexpected_out_of_bounds(uint64_t offset) {
    return std::unexpected(
        kernel::BusError{.kind = kernel::BusError::Kind::kOutOfBounds, .address = offset});
}

} // namespace micro_forge::device::pc
