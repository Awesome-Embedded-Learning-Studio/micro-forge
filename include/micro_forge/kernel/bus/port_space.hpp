/**
 * @file port_space.hpp
 * @brief The 16-bit port I/O space (x86 in/out routing).
 * @version 0.1.0
 *
 * Memory and ports are two different address spaces on x86: data goes
 * through the Bus, device registers through `in`/`out`. Devices register
 * a contiguous port window here and own the width handling; the space
 * only routes and refuses overlaps.
 */

#pragma once

#include <cstdint>
#include <expected>
#include <vector>

namespace micro_forge::kernel {

/// Why a port mapping was refused.
enum class PortError : uint8_t {
    kInvalidMapping, ///< Zero size or a 16-bit address wrap.
    kOverlap,        ///< The window intersects an existing device.
};

/**
 * @brief One device on the port space.
 *
 * The width parameter is the access width from the instruction (1, 2,
 * or 4); devices that only answer byte-wise may reject wider reads.
 */
class PortDevice {
public:
    virtual ~PortDevice() = default;

    /**
     * @brief Answers one port read.
     *
     * @param[in] offset Port offset inside the device window.
     * @param[in] width Access width in bytes.
     * @param[out] out The value read (zero-extended).
     * @return False when the device cannot serve this width.
     */
    virtual bool read(uint16_t offset, unsigned width, uint64_t& out) = 0;

    /**
     * @brief Answers one port write.
     *
     * @param[in] offset Port offset inside the device window.
     * @param[in] width Access width in bytes.
     * @param[in] value The value written.
     * @return False when the device cannot serve this width.
     */
    virtual bool write(uint16_t offset, unsigned width, uint64_t value) = 0;
};

/**
 * @brief The 16-bit port space: window registry plus routing.
 */
class PortSpace {
public:
    static constexpr uint32_t kPortLimit = 0x10000;

    /**
     * @brief Registers [base, base + size) for @p device.
     *
     * @param[in] base First port of the window.
     * @param[in] size Window size; must not be zero or wrap the space.
     * @param[in] device The device serving the window; caller owns it.
     * @return Success, or the refusal reason.
     */
    [[nodiscard]] std::expected<void, PortError> map(uint16_t base, uint16_t size,
                                                     PortDevice& device);

    /**
     * @brief Routes one read.
     *
     * @param[in] port Absolute port address.
     * @param[in] width Access width in bytes.
     * @param[out] out Value read on success.
     * @return False when no device covers @p port at this width.
     */
    [[nodiscard]] bool read(uint16_t port, unsigned width, uint64_t& out) const;

    /**
     * @brief Routes one write.
     *
     * @param[in] port Absolute port address.
     * @param[in] width Access width in bytes.
     * @param[in] value Value written.
     * @return False when no device covers @p port at this width.
     */
    bool write(uint16_t port, unsigned width, uint64_t value);

private:
    struct Entry {
        uint16_t base;
        uint16_t size;
        PortDevice* device;
    };
    /// Finds the entry covering [port, port + width), or nullptr.
    [[nodiscard]] const Entry* find(uint16_t port, unsigned width) const;

    std::vector<Entry> entries_;
};

} // namespace micro_forge::kernel
