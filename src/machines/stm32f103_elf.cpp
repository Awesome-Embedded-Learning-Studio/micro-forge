/**
 * @file stm32f103_elf.cpp
 * @brief Validated bare-metal ELF32 loading at physical load addresses.
 * @version 0.1.0
 */
#include "micro_forge/machines/stm32f103.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include "stm32f103_internal.hpp"

namespace micro_forge::machines {
namespace {

uint16_t le16(std::span<const uint8_t> bytes, std::size_t offset) {
    return static_cast<uint16_t>(static_cast<uint32_t>(bytes[offset]) |
                                 (static_cast<uint32_t>(bytes[offset + 1]) << 8U));
}

uint32_t le32(std::span<const uint8_t> bytes, std::size_t offset) {
    return uint32_t{bytes[offset]} | (uint32_t{bytes[offset + 1]} << 8U) |
           (uint32_t{bytes[offset + 2]} << 16U) | (uint32_t{bytes[offset + 3]} << 24U);
}

bool fits(uint64_t offset, uint64_t size, uint64_t extent) {
    return offset <= extent && size <= extent - offset;
}

struct Segment {
    uint64_t canonical;
    std::span<uint8_t> destination;
    std::span<uint8_t> zero;
    std::vector<uint8_t> source;
};

uint64_t canonical(uint64_t address) {
    return address <= Stm32F103Machine::kFlashSize ? Stm32F103Machine::kFlashBase + address
                                                   : address;
}

} // namespace

std::expected<void, Stm32Error> Stm32F103Machine::load_elf(std::span<const uint8_t> image) {
    using Kind = Stm32Error::Kind;
    const auto fail = [](Kind kind, uint64_t address) -> std::expected<void, Stm32Error> {
        return std::unexpected(Stm32Error{.kind = kind, .address = address});
    };
    if (image.size() < 52) {
        return fail(Kind::kTruncatedElf, image.size());
    }
    if (image[0] != 0x7F || image[1] != 'E' || image[2] != 'L' || image[3] != 'F') {
        return fail(Kind::kInvalidElf, 0);
    }
    if (image[4] != 1 || image[5] != 1 || image[6] != 1 || le16(image, 16) != 2 ||
        le16(image, 18) != 40 || le32(image, 20) != 1) {
        return fail(Kind::kUnsupportedElf, 4);
    }
    if (le16(image, 40) != 52 || le16(image, 42) != 32) {
        return fail(Kind::kInvalidElf, 40);
    }
    const uint64_t phoff = le32(image, 28);
    const uint64_t phnum = le16(image, 44);
    if (!fits(phoff, phnum * 32, image.size())) {
        return fail(Kind::kTruncatedElf, phoff);
    }
    std::vector<Segment> segments;
    for (uint64_t i = 0; i < phnum; ++i) {
        const auto header = image.subspan(static_cast<std::size_t>(phoff + (i * 32)), 32);
        if (le32(header, 0) != 1) {
            continue; // Only PT_LOAD is a physical firmware payload.
        }
        const uint64_t offset = le32(header, 4);
        const uint64_t vaddr = le32(header, 8);
        const uint64_t paddr = le32(header, 12);
        const uint64_t filesz = le32(header, 16);
        const uint64_t memsz = le32(header, 20);
        const uint64_t align = le32(header, 28);
        if (filesz > memsz || !fits(offset, filesz, image.size())) {
            return fail(Kind::kInvalidSegment, offset);
        }
        if (align > 1 &&
            ((align & (align - 1)) != 0 || (vaddr & (align - 1)) != (offset & (align - 1)))) {
            return fail(Kind::kInvalidSegment, phoff + (i * 32));
        }
        const auto runtime = impl_->image_window(vaddr, memsz);
        const auto destination = impl_->image_window(paddr, filesz);
        if (!runtime || !destination) {
            return fail(Kind::kUnmappedImage, !runtime ? vaddr : paddr);
        }
        std::span<uint8_t> zero;
        // A scatter-loaded .data segment's tail describes runtime SRAM, not
        // more Flash to program. Its startup code owns copying and BSS setup.
        // Zero a direct SRAM segment only; erased Flash stays erased.
        if (paddr == vaddr && vaddr >= kSramBase && vaddr <= kSramBase + kSramSize) {
            zero = runtime->subspan(static_cast<std::size_t>(filesz));
        }
        const uint64_t address = canonical(paddr);
        const uint64_t extent = filesz + zero.size();
        for (const auto& previous : segments) {
            const uint64_t previous_extent = previous.destination.size() + previous.zero.size();
            if (extent != 0 && previous_extent != 0 &&
                address < previous.canonical + previous_extent &&
                previous.canonical < address + extent) {
                return fail(Kind::kInvalidSegment, paddr);
            }
        }
        segments.push_back(Segment{
            .canonical = address,
            .destination = *destination,
            .zero = zero,
            .source =
                std::vector<uint8_t>(image.begin() + static_cast<std::ptrdiff_t>(offset),
                                     image.begin() + static_cast<std::ptrdiff_t>(offset + filesz)),
        });
    }
    if (segments.empty()) {
        return fail(Kind::kInvalidSegment, phoff);
    }
    // Validation of all headers, memory windows and overlaps precedes mutation.
    for (const auto& segment : segments) {
        std::ranges::copy(segment.source, segment.destination.begin());
        std::ranges::fill(segment.zero, uint8_t{0});
    }
    impl_->cpu.invalidate_instruction_cache();
    return {};
}

} // namespace micro_forge::machines
