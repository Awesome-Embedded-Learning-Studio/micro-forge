/**
 * @file snapshot.cpp
 * @brief Snapshot envelope writer/reader bodies (snapshot.hpp).
 * @version 0.1.0
 */

#include "micro_forge/kernel/snapshot.hpp"

#include "micro_forge/kernel/le_access.hpp"

#include <vector>

namespace micro_forge::kernel {

void SnapshotWriter::u32(std::uint32_t value) {
    const std::size_t at = payload_.size();
    payload_.resize(at + 4);
    kernel::put_le32(payload_.data() + at, value);
}

void SnapshotWriter::u64(std::uint64_t value) {
    const std::size_t at = payload_.size();
    payload_.resize(at + 8);
    kernel::put_le64(payload_.data() + at, value);
}

void SnapshotWriter::bytes(std::span<const std::uint8_t> data) {
    u64(data.size());
    payload_.insert(payload_.end(), data.begin(), data.end());
}

std::vector<std::uint8_t> SnapshotWriter::take() {
    std::vector<std::uint8_t> out;
    out.resize(header_size());
    kernel::put_le32(out.data(), kSnapshotMagic);
    kernel::put_le32(out.data() + 4, engine_version_);
    kernel::put_le32(out.data() + 8, kSnapshotFormatVersion);
    kernel::put_le64(out.data() + 12, payload_.size());
    out.insert(out.end(), payload_.begin(), payload_.end());
    return out;
}

std::expected<SnapshotReader, SnapshotError>
SnapshotReader::open(std::span<const std::uint8_t> blob, std::uint32_t expected_engine_version) {
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

std::optional<std::uint8_t> SnapshotReader::u8() {
    if (failed_ || pos_ >= bytes_.size()) {
        failed_ = true;
        return std::nullopt;
    }
    return bytes_[pos_++];
}

std::optional<std::uint32_t> SnapshotReader::u32() {
    if (failed_ || pos_ + 4 > bytes_.size()) {
        failed_ = true;
        return std::nullopt;
    }
    const std::uint32_t v = kernel::get_le32(bytes_.data() + pos_);
    pos_ += 4;
    return v;
}

std::optional<std::uint64_t> SnapshotReader::u64() {
    if (failed_ || pos_ + 8 > bytes_.size()) {
        failed_ = true;
        return std::nullopt;
    }
    const std::uint64_t v = kernel::get_le64(bytes_.data() + pos_);
    pos_ += 8;
    return v;
}

std::optional<std::vector<std::uint8_t>> SnapshotReader::bytes() {
    const std::uint64_t size = u64().value_or(0);
    if (failed_ || size > bytes_.size() - pos_) {
        failed_ = true;
        return std::nullopt;
    }
    std::vector<std::uint8_t> out(bytes_.begin() + static_cast<std::ptrdiff_t>(pos_),
                                  bytes_.begin() + static_cast<std::ptrdiff_t>(pos_ + size));
    pos_ += size;
    return out;
}

} // namespace micro_forge::kernel
