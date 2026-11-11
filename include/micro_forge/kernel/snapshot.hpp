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

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "micro_forge/kernel/le_access.hpp"

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
    void u32(std::uint32_t value) {
        const std::size_t at = payload_.size();
        payload_.resize(at + 4);
        kernel::put_le32(payload_.data() + at, value);
    }

    /// @brief Appends a little-endian 64-bit value.
    void u64(std::uint64_t value) {
        const std::size_t at = payload_.size();
        payload_.resize(at + 8);
        kernel::put_le64(payload_.data() + at, value);
    }

    /// @brief Appends a length-prefixed byte string.
    void bytes(std::span<const std::uint8_t> data) {
        u64(data.size());
        payload_.insert(payload_.end(), data.begin(), data.end());
    }

    /**
     * @brief Seals the blob: header triple plus payload.
     *
     * @return The full snapshot bytes.
     */
    [[nodiscard]] std::vector<std::uint8_t> take() {
        std::vector<std::uint8_t> out;
        out.resize(header_size());
        kernel::put_le32(out.data(), kSnapshotMagic);
        kernel::put_le32(out.data() + 4, engine_version_);
        kernel::put_le32(out.data() + 8, kSnapshotFormatVersion);
        kernel::put_le64(out.data() + 12, payload_.size());
        out.insert(out.end(), payload_.begin(), payload_.end());
        return out;
    }

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
    open(std::span<const std::uint8_t> blob, std::uint32_t expected_engine_version) {
        if (blob.size() < SnapshotWriter::header_size()) {
            return std::unexpected(SnapshotError::kTruncated);
        }
        const auto read_u32 = [&blob](std::size_t offset) {
            return kernel::get_le32(blob.data() + offset);
        };
        if (read_u32(0) != kSnapshotMagic) {
            return std::unexpected(SnapshotError::kBadMagic);
        }
        if (read_u32(4) != expected_engine_version) {
            return std::unexpected(SnapshotError::kEngineMismatch);
        }
        if (read_u32(8) != kSnapshotFormatVersion) {
            return std::unexpected(SnapshotError::kFormatMismatch);
        }
        const std::uint64_t payload_size = kernel::get_le64(blob.data() + 12);
        if (blob.size() - SnapshotWriter::header_size() != payload_size) {
            return std::unexpected(SnapshotError::kTruncated);
        }
        return SnapshotReader(blob.subspan(SnapshotWriter::header_size()));
    }

    /// @brief Reads one byte.
    ///
    /// @param[in,out] ok Set false when the payload is exhausted; sticky.
    /// @return The byte, or 0 after a failed read.
    std::uint8_t u8(bool& ok) {
        if (!ok || pos_ >= bytes_.size()) {
            ok = false;
            return 0;
        }
        return bytes_[pos_++];
    }

    /// @brief Reads a little-endian 32-bit value.
    ///
    /// @param[in,out] ok Sticky failure flag, see u8().
    /// @return The value, or 0 after a failed read.
    std::uint32_t u32(bool& ok) {
        if (!ok || pos_ + 4 > bytes_.size()) {
            ok = false;
            return 0;
        }
        const std::uint32_t v = kernel::get_le32(bytes_.data() + pos_);
        pos_ += 4;
        return v;
    }

    /// @brief Reads a little-endian 64-bit value.
    ///
    /// @param[in,out] ok Sticky failure flag, see u8().
    /// @return The value, or 0 after a failed read.
    std::uint64_t u64(bool& ok) {
        if (!ok || pos_ + 8 > bytes_.size()) {
            ok = false;
            return 0;
        }
        const std::uint64_t v = kernel::get_le64(bytes_.data() + pos_);
        pos_ += 8;
        return v;
    }

    /// @brief Unread payload bytes left (for bounding loop counts).
    [[nodiscard]] std::size_t remaining() const { return bytes_.size() - pos_; }

    /// @brief Reads a length-prefixed byte string.
    ///
    /// @param[in,out] ok Sticky failure flag, see u8().
    /// @return The bytes, or empty after a failed read.
    std::vector<std::uint8_t> bytes(bool& ok) {
        const std::uint64_t size = u64(ok);
        if (!ok || size > bytes_.size() - pos_) {
            ok = false;
            return {};
        }
        std::vector<std::uint8_t> out(bytes_.begin() + static_cast<std::ptrdiff_t>(pos_),
                                      bytes_.begin() + static_cast<std::ptrdiff_t>(pos_ + size));
        pos_ += size;
        return out;
    }

private:
    explicit SnapshotReader(std::span<const std::uint8_t> payload) : bytes_(payload) {}

    std::span<const std::uint8_t> bytes_;
    std::size_t pos_ = 0;
};

} // namespace micro_forge::kernel
