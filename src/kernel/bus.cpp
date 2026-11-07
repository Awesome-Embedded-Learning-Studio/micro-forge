#include "micro_forge/kernel/bus.hpp"

#include <algorithm>

namespace micro_forge::kernel {

std::expected<void, BusError> Bus::map(uint64_t base, uint64_t size, Region& region) {
    if (size == 0 || base + size < base) {
        return std::unexpected(BusError{.kind = BusError::Kind::kInvalidMapping, .address = base});
    }
    const uint64_t end = base + size;

    const auto it = std::ranges::upper_bound(mappings_, base, {}, &Mapping::base);

    if (it != mappings_.begin() && std::prev(it)->end > base) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOverlap, .address = base});
    }
    if (it != mappings_.end() && it->base < end) {
        return std::unexpected(BusError{.kind = BusError::Kind::kOverlap, .address = it->base});
    }

    mappings_.insert(it, Mapping{.base = base, .end = end, .region = &region});
    return {};
}

std::optional<Bus::Resolved> Bus::resolve(uint64_t address) const {
    auto it = std::ranges::upper_bound(mappings_, address, {}, &Mapping::base);
    if (it == mappings_.begin()) {
        return std::nullopt;
    }
    --it;
    if (address >= it->end) {
        return std::nullopt;
    }
    return Resolved{.region = it->region, .base = it->base, .offset = address - it->base};
}

namespace {

/// Region errors carry offsets; the bus converts them back to absolute guest
/// addresses before propagating.
template <typename T>
std::expected<T, BusError> rebase(std::expected<T, BusError> result, uint64_t base) {
    if (!result) {
        result.error().address += base;
    }
    return result;
}

} // namespace

std::expected<uint8_t, BusError> Bus::read_u8(uint64_t address) {
    const auto r = resolve(address);
    if (!r) {
        return std::unexpected(BusError{.kind = BusError::Kind::kUnmappedRead, .address = address});
    }
    return rebase(r->region->read_u8(r->offset), r->base);
}

std::expected<uint16_t, BusError> Bus::read_u16(uint64_t address) {
    const auto r = resolve(address);
    if (!r) {
        return std::unexpected(BusError{.kind = BusError::Kind::kUnmappedRead, .address = address});
    }
    return rebase(r->region->read_u16(r->offset), r->base);
}

std::expected<uint32_t, BusError> Bus::read_u32(uint64_t address) {
    const auto r = resolve(address);
    if (!r) {
        return std::unexpected(BusError{.kind = BusError::Kind::kUnmappedRead, .address = address});
    }
    return rebase(r->region->read_u32(r->offset), r->base);
}

std::expected<uint64_t, BusError> Bus::read_u64(uint64_t address) {
    const auto r = resolve(address);
    if (!r) {
        return std::unexpected(BusError{.kind = BusError::Kind::kUnmappedRead, .address = address});
    }
    return rebase(r->region->read_u64(r->offset), r->base);
}

std::expected<void, BusError> Bus::write_u8(uint64_t address, uint8_t value) {
    const auto r = resolve(address);
    if (!r) {
        return std::unexpected(
            BusError{.kind = BusError::Kind::kUnmappedWrite, .address = address});
    }
    return rebase(r->region->write_u8(r->offset, value), r->base);
}

std::expected<void, BusError> Bus::write_u16(uint64_t address, uint16_t value) {
    const auto r = resolve(address);
    if (!r) {
        return std::unexpected(
            BusError{.kind = BusError::Kind::kUnmappedWrite, .address = address});
    }
    return rebase(r->region->write_u16(r->offset, value), r->base);
}

std::expected<void, BusError> Bus::write_u32(uint64_t address, uint32_t value) {
    const auto r = resolve(address);
    if (!r) {
        return std::unexpected(
            BusError{.kind = BusError::Kind::kUnmappedWrite, .address = address});
    }
    return rebase(r->region->write_u32(r->offset, value), r->base);
}

std::expected<void, BusError> Bus::write_u64(uint64_t address, uint64_t value) {
    const auto r = resolve(address);
    if (!r) {
        return std::unexpected(
            BusError{.kind = BusError::Kind::kUnmappedWrite, .address = address});
    }
    return rebase(r->region->write_u64(r->offset, value), r->base);
}

} // namespace micro_forge::kernel
