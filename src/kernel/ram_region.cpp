#include "micro_forge/kernel/ram_region.hpp"
#include "micro_forge/kernel/le_access.hpp"

#include <algorithm>

namespace micro_forge::kernel {

namespace {

// The little-endian byte chains live in le_access.hpp since the decoder
// became the second consumer (batch-1 notes); RAM only picks the width.

} // namespace

std::optional<uint64_t> RamRegion::first_out_of_bounds(uint64_t offset, uint64_t width) const {
    if (offset >= bytes_.size() || bytes_.size() - offset < width) {
        return std::max(offset, bytes_.size());
    }
    return std::nullopt;
}

std::expected<uint8_t, BusError> RamRegion::read_u8(uint64_t offset) {
    if (const auto oob = first_out_of_bounds(offset, 1)) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = *oob});
    }
    return bytes_[offset];
}

std::expected<uint16_t, BusError> RamRegion::read_u16(uint64_t offset) {
    if (const auto oob = first_out_of_bounds(offset, 2)) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = *oob});
    }
    return kernel::get_le16(bytes_.data() + offset);
}

std::expected<uint32_t, BusError> RamRegion::read_u32(uint64_t offset) {
    if (const auto oob = first_out_of_bounds(offset, 4)) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = *oob});
    }
    return kernel::get_le32(bytes_.data() + offset);
}

std::expected<uint64_t, BusError> RamRegion::read_u64(uint64_t offset) {
    if (const auto oob = first_out_of_bounds(offset, 8)) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = *oob});
    }
    return kernel::get_le64(bytes_.data() + offset);
}

std::expected<void, BusError> RamRegion::write_u8(uint64_t offset, uint8_t value) {
    if (const auto oob = first_out_of_bounds(offset, 1)) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = *oob});
    }
    bytes_[offset] = value;
    return {};
}

std::expected<void, BusError> RamRegion::write_u16(uint64_t offset, uint16_t value) {
    if (const auto oob = first_out_of_bounds(offset, 2)) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = *oob});
    }
    kernel::put_le16(bytes_.data() + offset, value);
    return {};
}

std::expected<void, BusError> RamRegion::write_u32(uint64_t offset, uint32_t value) {
    if (const auto oob = first_out_of_bounds(offset, 4)) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = *oob});
    }
    kernel::put_le32(bytes_.data() + offset, value);
    return {};
}

std::expected<void, BusError> RamRegion::write_u64(uint64_t offset, uint64_t value) {
    if (const auto oob = first_out_of_bounds(offset, 8)) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = *oob});
    }
    kernel::put_le64(bytes_.data() + offset, value);
    return {};
}

} // namespace micro_forge::kernel
