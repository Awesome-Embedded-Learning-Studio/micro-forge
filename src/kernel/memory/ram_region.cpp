#include "micro_forge/kernel/memory/ram_region.hpp"
#include "micro_forge/kernel/le_access.hpp"

#include <algorithm>

namespace micro_forge::kernel {

namespace {

// The little-endian byte chains live in le_access.hpp since the decoder
// became the second consumer (batch-1 notes); RAM only picks the width.

} // namespace

std::optional<uint64_t> RamRegion::first_out_of_bounds(uint64_t offset, uint64_t width) const {
    if (offset >= size_ || size_ - offset < width) {
        return std::max(offset, size_);
    }
    return std::nullopt;
}

std::span<const uint8_t> RamRegion::peek(uint64_t offset, std::size_t len) const {
    return view(offset, len);
}

std::expected<uint8_t, BusError> RamRegion::read_u8(uint64_t offset) {
    if (const auto oob = first_out_of_bounds(offset, 1)) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = *oob});
    }
    return *ptr_at(offset);
}

std::expected<uint16_t, BusError> RamRegion::read_u16(uint64_t offset) {
    if (const auto oob = first_out_of_bounds(offset, 2)) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = *oob});
    }
    return kernel::get_le16(ptr_at(offset));
}

std::expected<uint32_t, BusError> RamRegion::read_u32(uint64_t offset) {
    if (const auto oob = first_out_of_bounds(offset, 4)) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = *oob});
    }
    return kernel::get_le32(ptr_at(offset));
}

std::expected<uint64_t, BusError> RamRegion::read_u64(uint64_t offset) {
    if (const auto oob = first_out_of_bounds(offset, 8)) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = *oob});
    }
    return kernel::get_le64(ptr_at(offset));
}

std::expected<void, BusError> RamRegion::write_u8(uint64_t offset, uint8_t value) {
    if (const auto oob = first_out_of_bounds(offset, 1)) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = *oob});
    }
    *ptr_at(offset) = value;
    return {};
}

std::expected<void, BusError> RamRegion::write_u16(uint64_t offset, uint16_t value) {
    if (const auto oob = first_out_of_bounds(offset, 2)) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = *oob});
    }
    kernel::put_le16(ptr_at(offset), value);
    return {};
}

std::expected<void, BusError> RamRegion::write_u32(uint64_t offset, uint32_t value) {
    if (const auto oob = first_out_of_bounds(offset, 4)) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = *oob});
    }
    kernel::put_le32(ptr_at(offset), value);
    return {};
}

std::expected<void, BusError> RamRegion::write_u64(uint64_t offset, uint64_t value) {
    if (const auto oob = first_out_of_bounds(offset, 8)) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOutOfBounds, .address = *oob});
    }
    kernel::put_le64(ptr_at(offset), value);
    return {};
}

void RamRegion::save(SnapshotWriter& writer) const {
    writer.u64(size_);
    writer.bytes(view(0, size_));
}

uint8_t* RamRegion::ptr_at(uint64_t offset) const {
    // Integer base -> pointer per access: in wasm window mode the base is
    // linear offset 0, and this shape (uintptr + offset, then cast) is the
    // only one the optimizer cannot fold into null-pointer arithmetic.
    // The integer->pointer cast is the point: in wasm window mode
    // base_ is 0 and this keeps stores off null-GEP poison.
    return reinterpret_cast<uint8_t*>(base_ + offset); // NOLINT(performance-no-int-to-ptr)
}

std::span<const uint8_t> RamRegion::view(uint64_t offset, uint64_t len) const {
    if (offset > size_ || len > size_ - offset) {
        return {};
    }
    // See ptr_at for the integer-base rationale.
    const auto* p =
        reinterpret_cast<const uint8_t*>(base_ + offset); // NOLINT(performance-no-int-to-ptr)
    return {p, static_cast<std::size_t>(len)};
}

bool RamRegion::copy_in(uint64_t offset, std::span<const uint8_t> src) {
    if (offset > size_ || src.size() > size_ - offset) {
        return false;
    }
    for (std::size_t i = 0; i < src.size(); ++i) {
        *ptr_at(offset + i) = src[i];
    }
    return true;
}

bool RamRegion::restore(SnapshotReader& reader) {
    bool ok = true;
    const uint64_t size = reader.u64(ok);
    if (!ok || size != size_) {
        return false; // mapping layout is config; sizes must agree
    }
    std::vector<uint8_t> data = reader.bytes(ok);
    if (!ok || data.size() != size_) {
        return false;
    }
    return copy_in(0, data);
}

} // namespace micro_forge::kernel
