/**
 * @file register_region.cpp
 * @brief Width plumbing for MMIO register windows (batch 6c).
 * @version 0.1.0
 */

#include "micro_forge/kernel/memory/register_region.hpp"

#include "micro_forge/kernel/le_access.hpp"

namespace micro_forge::kernel {

namespace {
/// Bounds check for one access (offset + width inside the window).
[[nodiscard]] bool fits(std::size_t size, uint64_t offset, unsigned width) {
    return offset <= size && width <= size - offset;
}

[[nodiscard]] std::unexpected<BusError> out_of_bounds(uint64_t offset) {
    return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = offset});
}
} // namespace

std::expected<uint8_t, BusError> RegisterRegionBase::read_u8(uint64_t offset) {
    on_read(offset, 1);
    if (!fits(size_, offset, 1)) {
        return out_of_bounds(offset);
    }
    return window_[offset];
}

std::expected<uint16_t, BusError> RegisterRegionBase::read_u16(uint64_t offset) {
    on_read(offset, 2);
    if (!fits(size_, offset, 2)) {
        return out_of_bounds(offset);
    }
    return get_le16(window_ + offset);
}

std::expected<uint32_t, BusError> RegisterRegionBase::read_u32(uint64_t offset) {
    on_read(offset, 4);
    if (!fits(size_, offset, 4)) {
        return out_of_bounds(offset);
    }
    return get_le32(window_ + offset);
}

std::expected<uint64_t, BusError> RegisterRegionBase::read_u64(uint64_t offset) {
    on_read(offset, 8);
    if (!fits(size_, offset, 8)) {
        return out_of_bounds(offset);
    }
    return get_le64(window_ + offset);
}

std::expected<void, BusError> RegisterRegionBase::write_u8(uint64_t offset, uint8_t value) {
    if (!fits(size_, offset, 1)) {
        return out_of_bounds(offset);
    }
    window_[offset] = value;
    on_write(offset, 1, value);
    return {};
}

std::expected<void, BusError> RegisterRegionBase::write_u16(uint64_t offset, uint16_t value) {
    if (!fits(size_, offset, 2)) {
        return out_of_bounds(offset);
    }
    put_le16(window_ + offset, value);
    on_write(offset, 2, value);
    return {};
}

std::expected<void, BusError> RegisterRegionBase::write_u32(uint64_t offset, uint32_t value) {
    if (!fits(size_, offset, 4)) {
        return out_of_bounds(offset);
    }
    put_le32(window_ + offset, value);
    on_write(offset, 4, value);
    return {};
}

std::expected<void, BusError> RegisterRegionBase::write_u64(uint64_t offset, uint64_t value) {
    if (!fits(size_, offset, 8)) {
        return out_of_bounds(offset);
    }
    put_le64(window_ + offset, value);
    on_write(offset, 8, value);
    return {};
}

} // namespace micro_forge::kernel
