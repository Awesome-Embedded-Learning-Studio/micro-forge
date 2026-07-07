#pragma once

#include "autogen/arch_details.hpp"
#include "periph/device.hpp"
#include "util/weak_ptr/weak_ptr.hpp"

namespace micro_forge::memory {

struct Region {
    addr_t start;
    addr_t end; // exclusive upper bound
    WeakPtr<periph::Device> device;
};

inline Region region(addr_t base, addr_t size, WeakPtr<periph::Device> dev) {
    return {base, base + size, std::move(dev)};
}

inline bool is_overlap(const Region& a, const Region& b) noexcept {
    return a.start < b.end && b.start < a.end;
}

} // namespace micro_forge::memory
