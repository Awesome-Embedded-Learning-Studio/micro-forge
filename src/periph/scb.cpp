#include "periph/scb.hpp"

namespace micro_forge::periph {

Expected<data_t> ScbPeripheral::read(addr_t offset, Width w) {
    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }

    switch (offset) {
        case 0x00:
            return cpuid_;
        case 0x04:
            return icsr_;
        case 0x08:
            return vtor_;
        case 0x0C:
            return aircr_;
        case 0x10:
            return scr_;
        case 0x14:
            return ccr_;
        case 0x18:
            return static_cast<uint32_t>(shp_[0]) | (shp_[1] << 8) |
                   (shp_[2] << 16) | (shp_[3] << 24);
        case 0x1C:
            return static_cast<uint32_t>(shp_[4]) | (shp_[5] << 8) |
                   (shp_[6] << 16) | (shp_[7] << 24);
        case 0x20:
            return static_cast<uint32_t>(shp_[8]) | (shp_[9] << 8) |
                   (shp_[10] << 16) | (shp_[11] << 24);
        case 0x24:
            return shcsr_;
        default:
            return std::unexpected(BusError::PeripheralFault);
    }
}

Expected<void> ScbPeripheral::write(addr_t offset, data_t data, Width w) {
    // SHP registers (offsets 0x18-0x23) support byte writes
    if (w == Width::Byte && offset >= 0x18 && offset <= 0x23) {
        shp_[offset - 0x18] = data & 0xFF;
        return {};
    }

    if (w != Width::Word) {
        return std::unexpected(BusError::Unaligned);
    }

    switch (offset) {
        case 0x04:
            icsr_ = data;
            return {};
        case 0x08:
            vtor_ = data;
            if (vtor_cb_) {
                vtor_cb_(data);
            }
            return {};
        case 0x0C:
            // AIRCR: must write VECTKEY = 0x05FA in upper 16 bits
            if ((data >> 16) != 0x05FA) {
                // ARM specifies writes with the wrong key are ignored; this is
                // an intentional hardware-compatible no-op, not a hidden fault.
                return {};
            }
            aircr_ = (data & 0x0000FFFFu) | 0xFA050000u;
            if (prigroup_cb_) {
                // PRIGROUP occupies AIRCR bits [10:8].
                prigroup_cb_(static_cast<uint8_t>((aircr_ >> 8) & 0x7u));
            }
            return {};
        case 0x10:
            scr_ = data;
            return {};
        case 0x14:
            ccr_ = data;
            return {};
        case 0x18:
            shp_[0] = data & 0xFF;
            shp_[1] = (data >> 8) & 0xFF;
            shp_[2] = (data >> 16) & 0xFF;
            shp_[3] = (data >> 24) & 0xFF;
            return {};
        case 0x1C:
            shp_[4] = data & 0xFF;
            shp_[5] = (data >> 8) & 0xFF;
            shp_[6] = (data >> 16) & 0xFF;
            shp_[7] = (data >> 24) & 0xFF;
            return {};
        case 0x20:
            shp_[8] = data & 0xFF;
            shp_[9] = (data >> 8) & 0xFF;
            shp_[10] = (data >> 16) & 0xFF;
            shp_[11] = (data >> 24) & 0xFF;
            return {};
        case 0x24:
            shcsr_ = data;
            return {};
        default:
            return std::unexpected(BusError::PeripheralFault);
    }
}

} // namespace micro_forge::periph
