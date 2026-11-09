/**
 * @file rtc.cpp
 * @brief CMOS clock semantics (batch 6b).
 * @version 0.1.0
 */

#include "micro_forge/kernel/rtc.hpp"

namespace micro_forge::kernel {

namespace {
/// Time registers of the image (index 0..9) plus status at 0x0A..0x0D.
constexpr uint8_t kStatusA = 0x0A;
constexpr uint8_t kStatusB = 0x0B;
constexpr uint8_t kStatusC = 0x0C;
constexpr uint8_t kStatusD = 0x0D;
} // namespace

bool Rtc::read(uint16_t offset, unsigned width, uint64_t& out) {
    if (width != 1) {
        return false;
    }
    if (offset == 0) {
        out = index_;
        return true;
    }
    if (offset != 1) {
        return false;
    }
    switch (index_) {
    case kStatusA:
        out = status_a_;
        break;
    case kStatusB:
        out = status_b_;
        break;
    case kStatusC:
        out = status_c_;
        break;
    case kStatusD:
        out = status_d_;
        break;
    default:
        out = index_ < registers_.size() ? registers_[index_] : 0;
        break;
    }
    return true;
}

bool Rtc::write(uint16_t offset, unsigned width, uint64_t value) {
    if (width != 1) {
        return false;
    }
    const auto byte = static_cast<uint8_t>(value);
    if (offset == 0) {
        index_ = static_cast<uint8_t>(byte & 0x7FU); // bit 7 = NMI, ignored
        return true;
    }
    if (offset != 1) {
        return false;
    }
    switch (index_) {
    case kStatusA:
        status_a_ = byte;
        break;
    case kStatusB:
        status_b_ = byte;
        break;
    case kStatusC: // read-clears on hardware; nothing latched yet
    case kStatusD: // read-only
        break;
    default:
        if (index_ < registers_.size()) {
            registers_[index_] = byte;
        }
        break;
    }
    return true;
}

} // namespace micro_forge::kernel
