#include "loader/elf_loader.hpp"
#include "autogen/arch_details.hpp"
#include "loader/binary_loader.hpp"
#include "loader/utils/def.h"
#include "loader/utils/elf_parser.hpp"

#include <cstring>
#include <string>

namespace micro_forge::loader {

namespace {

template <typename T> std::expected<T, std::string>
read_struct(std::span<const uint8_t> data, size_t offset) {
    if (offset + sizeof(T) > data.size()) {
        return std::unexpected("ELF data truncated at offset " +
                               std::to_string(offset));
    }
    T val;
    std::memcpy(&val, data.data() + offset, sizeof(T));
    return val;
}

bool add_overflows(uint32_t base, uint32_t size) {
    return size > (UINT32_MAX - base);
}

Width widest_width_for(size_t remaining) {
    if (remaining >= 4) {
        return Width::Word;
    }
    if (remaining >= 2) {
        return Width::HalfWord;
    }
    return Width::Byte;
}

uint32_t pack_little_endian(std::span<const uint8_t> data) {
    uint32_t word = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        word |= static_cast<uint32_t>(data[i]) << (i * 8);
    }
    return word;
}

std::expected<void, std::string> write_bytes(memory::Bus& bus, uint32_t addr,
                                             std::span<const uint8_t> data) {
    size_t offset = 0;
    while (offset < data.size()) {
        const auto width = widest_width_for(data.size() - offset);
        const auto chunk = static_cast<size_t>(width);
        if (add_overflows(addr, static_cast<uint32_t>(offset))) {
            return std::unexpected("segment address overflow");
        }

        const auto write_addr = addr + static_cast<uint32_t>(offset);
        const auto value = pack_little_endian(data.subspan(offset, chunk));
        auto res = bus.write(write_addr, value, width);
        if (!res.has_value()) {
            return std::unexpected(
                "failed to write segment at 0x" + ([](uint32_t v) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%08X", v);
                    return std::string(buf);
                })(write_addr));
        }

        offset += chunk;
    }

    return {};
}

std::expected<void, std::string>
write_segment(memory::Bus& bus, uint32_t addr,
              std::span<const uint8_t> seg_data, uint32_t memsz) {
    if (seg_data.size() > memsz) {
        return std::unexpected("PT_LOAD filesz exceeds memsz");
    }
    if (add_overflows(addr, memsz)) {
        return std::unexpected("PT_LOAD segment address overflow");
    }

    auto write_result = write_bytes(bus, addr, seg_data);
    if (!write_result) {
        return write_result;
    }

    // Zero-fill BSS (memsz > filesz)
    size_t offset = seg_data.size();
    while (offset < memsz) {
        const auto width = widest_width_for(memsz - offset);
        const auto write_addr = addr + static_cast<uint32_t>(offset);
        auto res = bus.write(write_addr, 0, width);
        if (!res.has_value()) {
            return std::unexpected("failed to zero-fill BSS at offset " +
                                   std::to_string(offset));
        }
        offset += static_cast<size_t>(width);
    }

    return {};
}

} // anonymous namespace

std::expected<ElfLoadResult, std::string>
load_elf(memory::Bus& bus, std::span<const uint8_t> elf_data) {
    using namespace utils;

    // Validate ELF header
    auto mut_span = std::span<uint8_t>(const_cast<uint8_t*>(elf_data.data()),
                                       elf_data.size());
    if (!is_valid_elf(mut_span)) {
        return std::unexpected("invalid ELF magic");
    }
    if (auto r = is_elf32(mut_span); !r) {
        return std::unexpected("not a 32-bit ELF");
    }
    if (auto r = is_le(mut_span); !r) {
        return std::unexpected("not little-endian ELF");
    }
    if (auto r = is_arm(mut_span); !r) {
        return std::unexpected("not an ARM ELF");
    }

    // Read ELF header
    auto ehdr = read_struct<Elf32_Ehdr>(elf_data, 0);
    if (!ehdr) {
        return std::unexpected(ehdr.error());
    }

    static_assert(sizeof(Elf32_Ehdr) == 52);

    // Iterate program headers
    for (uint16_t i = 0; i < ehdr->e_phnum; ++i) {
        size_t ph_offset =
            ehdr->e_phoff + static_cast<size_t>(i) * ehdr->e_phentsize;
        auto phdr = read_struct<Elf32_Phdr>(elf_data, ph_offset);
        if (!phdr) {
            return std::unexpected(phdr.error());
        }

        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        // Extract segment data
        if (add_overflows(phdr->p_offset, phdr->p_filesz) ||
            static_cast<size_t>(phdr->p_offset) + phdr->p_filesz >
                elf_data.size()) {
            return std::unexpected("PT_LOAD segment data truncated");
        }

        auto seg_data = elf_data.subspan(phdr->p_offset, phdr->p_filesz);

        auto res = write_segment(bus, phdr->p_vaddr, seg_data, phdr->p_memsz);
        if (!res) {
            return std::unexpected(res.error());
        }
    }

    return ElfLoadResult{.entry_point = ehdr->e_entry};
}

} // namespace micro_forge::loader
