/**
 * @file port_space.cpp
 * @brief Port space routing (batch 6b).
 * @version 0.1.0
 */

#include "micro_forge/kernel/bus/port_space.hpp"

#include <cstdint>

namespace micro_forge::kernel {

std::expected<void, PortError> PortSpace::map(uint16_t base, uint16_t size, PortDevice& device) {
    if (size == 0) {
        return std::unexpected(PortError::kInvalidMapping);
    }
    const uint32_t end = static_cast<uint32_t>(base) + size;
    if (end > kPortLimit) {
        return std::unexpected(PortError::kInvalidMapping); // 16-bit wrap
    }
    for (const Entry& entry : entries_) {
        const uint32_t entry_end = static_cast<uint32_t>(entry.base) + entry.size;
        if (base < entry_end && entry.base < end) {
            return std::unexpected(PortError::kOverlap);
        }
    }
    entries_.push_back(Entry{.base = base, .size = size, .device = &device});
    return {};
}

const PortSpace::Entry* PortSpace::find(uint16_t port, unsigned width) const {
    if (width == 0 || width > 4) {
        return nullptr;
    }
    const uint32_t end = static_cast<uint32_t>(port) + width;
    if (end > kPortLimit) {
        return nullptr;
    }
    for (const Entry& entry : entries_) {
        const uint32_t entry_end = static_cast<uint32_t>(entry.base) + entry.size;
        if (port >= entry.base && end <= entry_end) {
            return &entry;
        }
    }
    return nullptr;
}

bool PortSpace::read(uint16_t port, unsigned width, uint64_t& out) const {
    const Entry* entry = find(port, width);
    if (entry == nullptr) {
        return false;
    }
    return entry->device->read(static_cast<uint16_t>(port - entry->base), width, out);
}

bool PortSpace::write(uint16_t port, unsigned width, uint64_t value) {
    const Entry* entry = find(port, width);
    if (entry == nullptr) {
        return false;
    }
    return entry->device->write(static_cast<uint16_t>(port - entry->base), width, value);
}

} // namespace micro_forge::kernel
