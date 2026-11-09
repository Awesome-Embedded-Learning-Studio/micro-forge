/**
 * @file cinux_loader.hpp
 * @brief The boot shim: ImageHeader, E820, page tables, BootInfo (6c).
 * @version 0.1.0
 *
 * Cinux expects the world switch already done: a 64-bit kernel mapped
 * at the high half (0xFFFFFFFF80000000 + physical), an identity-mapped
 * low gigabyte, CR3 pointing at that page tree, EFER in long mode, RSP
 * on the boot stack, and RDI holding the virtual address of a BootInfo
 * record. This module plays the role of the boot loader's long-mode
 * stage without the 16/32-bit part: it parses the image header, places
 * the image at its load address, builds E820 + BootInfo, installs the
 * page tables, and hands the CPU to the kernel entry.
 */

#pragma once

#include <cstdint>
#include <expected>
#include <span>

#include "micro_forge/arch/x86_64/cpu.hpp"
#include "micro_forge/kernel/ram_region.hpp"

namespace micro_forge::loader {

/// One ImageHeader as the Cinux build stamps it (40 bytes, packed).
struct ImageHeader {
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t header_size = 0;
    uint64_t load_paddr = 0;
    uint64_t file_size = 0;
    uint64_t mem_size = 0;
    uint64_t entry = 0;
};

/// Image magic this loader demands (Cinux kernel/boot/image_header.hpp).
inline constexpr uint32_t kImageMagic = 0x00114514;
/// Header version this loader understands.
inline constexpr uint16_t kImageVersion = 1;
/// The high-half base the kernel links against.
inline constexpr uint64_t kHighHalfBase = 0xFFFFFFFF80000000ULL;
/// Boot stack top the long-mode stage leaves RSP at.
inline constexpr uint64_t kBootStackTop = 0x90000;
/// Where the BootInfo record lives (physical, below the kernel image).
inline constexpr uint64_t kBootInfoPhysical = 0x800000;
/// Page-table pages used by the shim (physical, low memory).
inline constexpr uint64_t kPml4Physical = 0x1000;
inline constexpr uint64_t kPdptLowPhysical = 0x2000;
inline constexpr uint64_t kPdLowPhysical = 0x3000;
inline constexpr uint64_t kPdptHighPhysical = 0x4000;
inline constexpr uint64_t kPdHighPhysical = 0x5000;

/// Why a boot attempt was refused.
enum class BootError : uint8_t {
    kImageTooSmall,    ///< Fewer bytes than the header.
    kBadMagic,         ///< Image magic mismatch.
    kBadVersion,       ///< Header version mismatch.
    kBadHeaderSize,    ///< Header size smaller than the struct.
    kImageOutOfRam,    ///< The image would not fit guest RAM.
    kBootInfoOutOfRam, ///< The BootInfo placement would not fit.
};

/**
 * @brief Parses and validates one in-memory ImageHeader.
 *
 * @param[in] image The kernel image bytes.
 * @return The header, or the refusal reason.
 */
std::expected<ImageHeader, BootError> parse_image_header(std::span<const uint8_t> image);

/**
 * @brief Runs the boot shim: places the image, builds E820 + BootInfo,
 *        installs the identity + high-half page tables, and hands the
 *        CPU to the kernel entry (RDI = BootInfo, RSP = boot stack).
 *
 * @param[in] ram Guest RAM region (the loader writes through it).
 * @param[in] cpu The CPU to hand over.
 * @param[in] image The kernel image bytes.
 * @return Success, or the refusal reason.
 */
std::expected<void, BootError> boot_cinux(micro_forge::kernel::RamRegion& ram,
                                          micro_forge::arch::x86_64::Cpu& cpu,
                                          std::span<const uint8_t> image);

} // namespace micro_forge::loader
