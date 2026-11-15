/**
 * @file snapshot.hpp
 * @brief Versioned snapshot blob: writer/reader over the engine/version/
 *        format triple.
 * @version 0.1.0
 *
 * The triple (spec 5.5): magic names the format family, the engine
 * version is the micro-forge version that produced the blob, and the
 * format version is this layout's own version. A reader that cannot
 * match all three refuses the blob instead of guessing.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

namespace micro_forge::kernel {

/// @brief Blob family tag: "MFSN" (micro-forge snapshot).
inline constexpr std::uint32_t kSnapshotMagic = 0x4E53464DU;

/// @brief Layout version of the snapshot envelope and section encoding.
inline constexpr std::uint32_t kSnapshotFormatVersion = 2;

/// @brief Why a snapshot was refused at open time.
enum class SnapshotError : std::uint8_t {
    kBadMagic,       ///< The blob is not a micro-forge snapshot.
    kFormatMismatch, ///< Blob layout version is not readable here.
    kEngineMismatch, ///< Blob came from a different engine version.
    kTruncated,      ///< Header or payload length disagrees with the bytes.
};

/// @brief Packed engine version for the snapshot header
///        (major << 16 | minor << 8 | patch).
[[nodiscard]] constexpr std::uint32_t pack_engine_version(unsigned major, unsigned minor,
                                                          unsigned patch) {
    return (major << 16U) | (minor << 8U) | patch;
}

/**
 * @brief Little-endian payload writer behind a versioned header.
 *
 * Sections are written by their owners (RAM, CPU, scheduler, devices);
 * this class only owns the envelope and the primitive encodings.
 */
class SnapshotWriter {
public:
    /**
     * @brief Starts a blob stamped with @p engine_version.
     *
     * @param[in] engine_version Packed engine version (see
     *            pack_engine_version).
     */
    explicit SnapshotWriter(std::uint32_t engine_version) : engine_version_(engine_version) {}

    /// @brief Appends one byte.
    void u8(std::uint8_t value) { payload_.push_back(value); }

    /// @brief Appends a little-endian 32-bit value.
    void u32(std::uint32_t value);

    /// @brief Appends a little-endian 64-bit value.
    void u64(std::uint64_t value);

    /// @brief Appends a length-prefixed byte string.
    void bytes(std::span<const std::uint8_t> data);

    /**
     * @brief Seals the blob: header triple plus payload.
     *
     * @return The full snapshot bytes.
     */
    [[nodiscard]] std::vector<std::uint8_t> take();

    /// @brief Envelope size in bytes (magic, versions, payload length).
    [[nodiscard]] static constexpr std::size_t header_size() { return 4U + 4U + 4U + 8U; }

private:
    std::uint32_t engine_version_;
    std::vector<std::uint8_t> payload_;
};

/**
 * @brief Validating reader over a sealed snapshot blob.
 *
 * Construction checks the whole triple plus the payload length before
 * any section decoding starts; owners then read their sections in the
 * same order they wrote them.
 */
class SnapshotReader {
public:
    /**
     * @brief Validates @p blob against @p expected_engine_version.
     *
     * @param[in] blob Full snapshot bytes (header plus payload).
     * @param[in] expected_engine_version The reading engine's packed
     *            version; a mismatch refuses the blob.
     * @return A positioned reader, or the refusal reason.
     */
    [[nodiscard]] static std::expected<SnapshotReader, SnapshotError>
    open(std::span<const std::uint8_t> blob, std::uint32_t expected_engine_version);

    /// @brief Reads one byte.
    /// @return The byte; empty once the payload is exhausted. The failure
    ///         is sticky: every later read stays empty.
    std::optional<std::uint8_t> u8();

    /// @brief Reads a little-endian 32-bit value.
    /// @return The value; empty per the sticky failure contract of u8().
    std::optional<std::uint32_t> u32();

    /// @brief Reads a little-endian 64-bit value.
    /// @return The value; empty per the sticky failure contract of u8().
    std::optional<std::uint64_t> u64();

    /// @brief Unread payload bytes left (for bounding loop counts).
    [[nodiscard]] std::size_t remaining() const { return bytes_.size() - pos_; }

    /// @brief Reads a length-prefixed byte string.
    /// @return The bytes; empty per the sticky failure contract of u8().
    std::optional<std::vector<std::uint8_t>> bytes();

private:
    explicit SnapshotReader(std::span<const std::uint8_t> payload) : bytes_(payload) {}

    std::span<const std::uint8_t> bytes_;
    std::size_t pos_ = 0;
    /// Sticky failure: set on the first short read, never cleared.
    bool failed_ = false;
};

} // namespace micro_forge::kernel
