#include "loader/utils/elf_parser.hpp"
#include "loader/utils/elf_defs.hpp"
#include <algorithm>

namespace micro_forge::loader::utils {
bool is_valid_elf(const std::span<uint8_t>& span_data) {
    if (span_data.size() < 4) {
        return false;
    }

    if (!std::ranges::equal(span_data.first<4>(), ELF_MAGIC)) {
        return false;
    }

    return true;
}

std::expected<void, ElfLoadError>
is_elf32(const std::span<uint8_t>& span_data) {
    if (span_data.size() < 5) {
        return std::unexpected(ElfLoadError::ElfValidationFailed);
    }
    if (span_data[4] != ELFCLASS32) {
        return std::unexpected(ElfLoadError::NotELF32);
    }
    return {};
}

std::expected<void, ElfLoadError> is_le(const std::span<uint8_t>& span_data) {
    if (span_data.size() < 6) {
        return std::unexpected(ElfLoadError::ElfValidationFailed);
    }
    if (span_data[5] != ELFDATA2LSB) {
        return std::unexpected(ElfLoadError::NotLE);
    }
    return {};
}

std::expected<void, ElfLoadError> is_arm(const std::span<uint8_t>& span_data) {
    if (span_data.size() < 20) {
        return std::unexpected(ElfLoadError::ElfValidationFailed);
    }
    uint16_t machine = static_cast<uint16_t>(span_data[18]) |
                       (static_cast<uint16_t>(span_data[19]) << 8);
    if (machine != EM_ARM) {
        return std::unexpected(ElfLoadError::NotTargetArch);
    }
    return {};
}

} // namespace micro_forge::loader::utils