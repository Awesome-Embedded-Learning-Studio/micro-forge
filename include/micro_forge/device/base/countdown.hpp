/**
 * @file countdown.hpp
 * @brief Shared down-counter engine behind SysTick-style periodic timers.
 * @version 0.1.0
 *
 * Owns the reload shadow, the current value, the wrap ("expired") state
 * and the wrap-callback edge. The register map stays with its device:
 * flag placement (COUNTFLAG, UIF) and interrupt-enable gating are
 * device-side translations of register accesses.
 */
#pragma once

#include <cstdint>
#include <functional>

namespace micro_forge::device {

/** @brief Down-counter with auto-reload shared by periodic timer models. */
class CountdownCore {
public:
    struct Config {
        uint32_t max_reload; ///< Reload mask (0xFFFFFF for SysTick, 0xFFFF for 16-bit timers).
    };
    using ExpireCallback = std::function<void()>;

    /**
     * @brief Builds the engine for one counter width.
     * @param[in] config Reload mask for every programmed reload value.
     */
    explicit CountdownCore(Config config);

    /// @brief Clears reload, value and wrap state; the callback stays wired.
    void reset();

    /**
     * @brief Programs the reload value; masked to the configured width.
     * @param[in] reload Raw register value.
     */
    void set_reload(uint32_t reload);

    /// @brief The masked reload shadow.
    [[nodiscard]] uint32_t reload() const;

    /**
     * @brief Implements a value-register write: clears the counter and wrap state.
     */
    void clear_value();

    /**
     * @brief Consumes input-clock ticks; fires the callback on every wrap.
     *
     * A reload from zero consumes one tick before counting down, and a
     * zero reload reports one wrap and then waits for reprogramming.
     * @param[in] cycles Input-clock ticks available this step.
     */
    void tick(uint64_t cycles);

    /// @brief The current counter value.
    [[nodiscard]] uint32_t value() const;

    /// @brief True since the last wrap until cleared.
    [[nodiscard]] bool expired() const;

    /// @brief Clears the wrap state (flag-read-to-clear semantics).
    void clear_expired();

    /// @brief Wires the wrap callback; the device gates it by its own enable bits.
    void set_expire_callback(ExpireCallback callback);

private:
    Config config_;
    uint32_t load_ = 0;
    uint32_t val_ = 0;
    bool expired_ = false;
    ExpireCallback expire_callback_;
};

} // namespace micro_forge::device
