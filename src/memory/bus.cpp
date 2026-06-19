#include "memory/bus.hpp"

namespace micro_forge::memory {

Expected<void> Bus::map(Region region) {
    if (!region.device.IsValid()) {
        return std::unexpected(BusError::InvalidDevice);
    }

    for (const auto& existing : regions_) {
        if (is_overlap(region, existing)) {
            return std::unexpected(BusError::RegionOverlap);
        }
    }

    regions_.push_back(std::move(region));
    return {};
}

Region* Bus::find_region(addr_t addr) {
    for (auto& r : regions_) {
        if (addr >= r.start && addr < r.end) {
            return &r;
        }
    }
    return nullptr;
}

void Bus::trace_access(bool is_write, addr_t addr, data_t value, Width width,
                       Expected<void> result, std::string_view device) {
    if (!trace_) {
        return;
    }
    BusTraceEvent event{
        .is_write = is_write,
        .addr = addr,
        .value = value,
        .width = width,
        .ok = result.has_value(),
        .error = result.has_value() ? BusError::Unmapped : result.error(),
        .device = device,
    };
    trace_(event);
}

Expected<data_t> Bus::read(addr_t addr, Width w) {
    // Bit-band alias translation (Cortex-M3 hardware feature).
    if (addr >= 0x2200'0000u && addr < 0x2400'0000u) {
        return bitband_read(addr, 0x2200'0000u, 0x2000'0000u);
    }
    if (addr >= 0x4200'0000u && addr < 0x4400'0000u) {
        return bitband_read(addr, 0x4200'0000u, 0x4000'0000u);
    }
    auto* region = find_region(addr);
    if (!region) {
        trace_access(false, addr, 0, w, std::unexpected(BusError::Unmapped),
                     "unmapped");
        return std::unexpected(BusError::Unmapped);
    }
    if (!region->device.IsValid()) {
        trace_access(false, addr, 0, w,
                     std::unexpected(BusError::InvalidDevice), "invalid");
        return std::unexpected(BusError::InvalidDevice);
    }
    auto result = region->device->read(addr - region->start, w);
    auto trace_result = result.has_value()
                            ? Expected<void>{}
                            : Expected<void>{std::unexpected(result.error())};
    trace_access(false, addr, result.value_or(0), w, trace_result,
                 region->device->name());
    return result;
}

Expected<void> Bus::write(addr_t addr, data_t data, Width w) {
    // Bit-band alias translation (Cortex-M3 hardware feature).
    if (addr >= 0x2200'0000u && addr < 0x2400'0000u) {
        return bitband_write(addr, 0x2200'0000u, 0x2000'0000u, data);
    }
    if (addr >= 0x4200'0000u && addr < 0x4400'0000u) {
        return bitband_write(addr, 0x4200'0000u, 0x4000'0000u, data);
    }
    auto* region = find_region(addr);
    if (!region) {
        trace_access(true, addr, data, w, std::unexpected(BusError::Unmapped),
                     "unmapped");
        return std::unexpected(BusError::Unmapped);
    }
    if (!region->device.IsValid()) {
        trace_access(true, addr, data, w,
                     std::unexpected(BusError::InvalidDevice), "invalid");
        return std::unexpected(BusError::InvalidDevice);
    }
    auto result = region->device->write(addr - region->start, data, w);
    trace_access(true, addr, data, w, result, region->device->name());
    return result;
}

Expected<data_t> Bus::bitband_read(addr_t addr, addr_t band_base,
                                   addr_t region_base) {
    // alias_addr = band_base + (byte_offset << 5) + (bit << 2).
    // A word read returns 1 if the target bit is set, else 0.
    uint32_t offset = static_cast<uint32_t>(addr - band_base);
    addr_t target_byte = region_base + (offset >> 5);
    uint32_t bit_in_word =
        ((target_byte & 0x3u) * 8u) + ((offset & 0x1Fu) >> 2);
    auto word = read(target_byte & ~0x3u, Width::Word);
    if (!word) {
        return std::unexpected(word.error());
    }
    return (*word >> bit_in_word) & 1u;
}

Expected<void> Bus::bitband_write(addr_t addr, addr_t band_base,
                                  addr_t region_base, data_t data) {
    // Atomic read-modify-write of the target bit: non-zero writes set the
    // bit, zero clears it. Operates on the containing word so peripheral
    // register writes stay word-aligned.
    uint32_t offset = static_cast<uint32_t>(addr - band_base);
    addr_t target_byte = region_base + (offset >> 5);
    uint32_t bit_in_word =
        ((target_byte & 0x3u) * 8u) + ((offset & 0x1Fu) >> 2);
    auto word = read(target_byte & ~0x3u, Width::Word);
    if (!word) {
        return std::unexpected(word.error());
    }
    uint32_t mask = 1u << bit_in_word;
    uint32_t new_word = (data != 0) ? (*word | mask) : (*word & ~mask);
    return write(target_byte & ~0x3u, new_word, Width::Word);
}

} // namespace micro_forge::memory
