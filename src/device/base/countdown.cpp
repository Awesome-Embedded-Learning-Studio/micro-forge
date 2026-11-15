/**
 * @file countdown.cpp
 * @brief Down-counter engine shared by SysTick-style periodic timers.
 * @version 0.1.0
 */
#include "micro_forge/device/base/countdown.hpp"

#include <utility>

namespace micro_forge::device {

CountdownCore::CountdownCore(Config config) : config_(config) {}

void CountdownCore::reset() {
    load_ = 0;
    val_ = 0;
    expired_ = false;
}

void CountdownCore::set_reload(uint32_t reload) {
    load_ = reload & config_.max_reload;
}

uint32_t CountdownCore::reload() const {
    return load_;
}

void CountdownCore::clear_value() {
    val_ = 0;
    expired_ = false;
}

void CountdownCore::tick(uint64_t cycles) {
    while (cycles > 0) {
        if (val_ == 0) {
            if (load_ == 0) {
                return; // Idle at zero with no reload: no wrap, no callback.
            }
            val_ = load_; // Reloading from zero consumes one input-clock tick.
            --cycles;
        }
        if (cycles < val_) {
            val_ -= static_cast<uint32_t>(cycles);
            return;
        }
        cycles -= val_;
        val_ = 0;
        expired_ = true;
        if (expire_callback_) {
            expire_callback_();
        }
    }
}

uint32_t CountdownCore::value() const {
    return val_;
}

bool CountdownCore::expired() const {
    return expired_;
}

void CountdownCore::clear_expired() {
    expired_ = false;
}

void CountdownCore::set_expire_callback(ExpireCallback callback) {
    expire_callback_ = std::move(callback);
}

} // namespace micro_forge::device
