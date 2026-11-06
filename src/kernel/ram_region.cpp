#include "micro_forge/kernel/ram_region.hpp"
#include "micro_forge/kernel/le_access.hpp"

#include <algorithm>

namespace micro_forge::kernel {

namespace {

// Little-endian byte chains in the shape of Linux's
// include/linux/unaligned/le_byteshift.h: one helper per width, straight
// loads and stores instead of recursive halfword composition. Unlike the
// kernel originals, every shift enters the unsigned domain first: a plain
// p[3] << 24 promotes to int and only stays defined through the modulo
// conversion rule ([expr.shift] with C++20 two's complement semantics), so
// keeping each intermediate representable makes the code state its own
// correctness instead of leaning on that clause.
void put_le16(uint8_t* bytes, uint16_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8U);
}

void put_le32(uint8_t* bytes, uint32_t value) {
    bytes[0] = static_cast<uint8_t>(value);
    bytes[1] = static_cast<uint8_t>(value >> 8U);
    bytes[2] = static_cast<uint8_t>(value >> 16U);
    bytes[3] = static_cast<uint8_t>(value >> 24U);
}

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
    put_le16(bytes_.data() + offset, value);
    return {};
}

std::expected<void, BusError> RamRegion::write_u32(uint64_t offset, uint32_t value) {
    if (const auto oob = first_out_of_bounds(offset, 4)) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = *oob});
    }
    put_le32(bytes_.data() + offset, value);
    return {};
}

} // namespace micro_forge::kernel
