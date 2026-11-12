/**
 * @file cinux_loader.cpp
 * @brief Boot shim implementation (batch 6c).
 * @version 0.1.0
 *
 * Wire layouts mirror kernel/boot/image_header.hpp and boot_info.hpp;
 * both are packed, so the byte writes here are the field order Cinux
 * reads. E820 reports one usable region above 1 MiB (the low megabyte
 * stays reserved: IVT/EBDA/BootInfo/stack all live there).
 */

#include "micro_forge/loader/cinux_loader.hpp"

#include <array>
#include <cstring>
#include <expected>

#include "micro_forge/arch/x86_64/cpu.hpp"
#include "micro_forge/kernel/device/acpi_tables.hpp"
#include "micro_forge/kernel/le_access.hpp"
#include "micro_forge/kernel/memory/ram_region.hpp"

namespace micro_forge::loader {

namespace {

constexpr uint64_t kHeaderBytes = 40;
constexpr uint64_t kWireStructBytes = 40; // ImageHeader on the wire
constexpr uint64_t kLowMegabyte = 0x100000;

/// The BootInfo wire layout (kernel/boot/boot_info.hpp, packed).
// FramebufferInfo is 48 bytes (8 + 4*4 + 6*4), not 40.
constexpr uint64_t kFramebufferBytes = 48;
constexpr uint64_t kBootInfoBytes = 4 + 4 + 4 + 4 + (128 * 20) + kFramebufferBytes + 8 + 8 + 8 + 8;

// Writes go through view(): window mode (the wasm artifact) has no owning
// storage, and data() there is a null pointer whose stores -O2 may delete.
void put_u32(micro_forge::kernel::RamRegion& ram, uint64_t at, uint32_t value) {
    std::array<uint8_t, 4> bytes{};
    for (unsigned i = 0; i < 4; ++i) {
        bytes[i] = static_cast<uint8_t>(value >> (8U * i));
    }
    (void)ram.copy_in(at, bytes);
}

void put_u64(micro_forge::kernel::RamRegion& ram, uint64_t at, uint64_t value) {
    std::array<uint8_t, 8> bytes{};
    for (unsigned i = 0; i < 8; ++i) {
        bytes[i] = static_cast<uint8_t>(value >> (8U * i));
    }
    (void)ram.copy_in(at, bytes);
}

void put_page(micro_forge::kernel::RamRegion& ram, uint64_t at, uint64_t value) {
    put_u64(ram, at, value);
}

/// One table page: 512 entries of @p entry_base addresses.
void fill_table_page(micro_forge::kernel::RamRegion& ram, uint64_t page, uint64_t first_phys,
                     bool huge_pages) {
    for (uint64_t i = 0; i < 512; ++i) {
        const uint64_t address = first_phys + (i * (huge_pages ? 0x200000ULL : 0x1000ULL));
        // Present | Write | (Shared bit clear); huge pages add PS.
        const uint64_t flags = 0x3ULL | (huge_pages ? 0x80ULL : 0);
        put_page(ram, page + (i * 8U), address | flags);
    }
}

} // namespace

std::expected<ImageHeader, BootError> parse_image_header(std::span<const uint8_t> image) {
    if (image.size() < kHeaderBytes) {
        return std::unexpected(BootError::kImageTooSmall);
    }
    ImageHeader header;
    header.magic = micro_forge::kernel::get_le32(image.data());
    header.version = micro_forge::kernel::get_le16(image.data() + 4);
    header.header_size = micro_forge::kernel::get_le16(image.data() + 6);
    header.load_paddr = micro_forge::kernel::get_le64(image.data() + 8);
    header.file_size = micro_forge::kernel::get_le64(image.data() + 16);
    header.mem_size = micro_forge::kernel::get_le64(image.data() + 24);
    header.entry = micro_forge::kernel::get_le64(image.data() + 32);
    if (header.magic != kImageMagic) {
        return std::unexpected(BootError::kBadMagic);
    }
    if (header.version != kImageVersion) {
        return std::unexpected(BootError::kBadVersion);
    }
    if (header.header_size < kWireStructBytes) {
        return std::unexpected(BootError::kBadHeaderSize);
    }
    return header;
}

std::expected<void, BootError> boot_cinux(micro_forge::kernel::RamRegion& ram,
                                          micro_forge::arch::x86_64::Cpu& cpu,
                                          std::span<const uint8_t> image) {
    const auto parsed = parse_image_header(image);
    if (!parsed.has_value()) {
        return std::unexpected(parsed.error());
    }
    const ImageHeader header = *parsed;
    // Start from the reset state: MSR defaults (APIC_BASE included),
    // flags, and the clear console belong to a fresh machine.
    cpu.reset(kBootStackTop, kBootStackTop);
    // The image must fit guest RAM with its mem_size reservation.
    if (header.load_paddr > ram.size() || header.mem_size > ram.size() - header.load_paddr ||
        header.file_size > header.mem_size) {
        return std::unexpected(BootError::kImageOutOfRam);
    }
    if (kBootInfoPhysical + kBootInfoBytes > ram.size()) {
        return std::unexpected(BootError::kBootInfoOutOfRam);
    }

    // Place the image at its load address (the file bytes only; the
    // mem_size tail stays zeroed as BSS).
    (void)ram.copy_in(header.load_paddr, image.subspan(0, header.file_size));

    // ---- Page tables: identity [0, 1 GiB) + high half [0, 1 GiB). ----
    // PML4[0] -> low PDPT -> low PD (2 MiB pages).
    put_page(ram, kPml4Physical, kPdptLowPhysical | 0x3ULL);
    put_page(ram, kPdptLowPhysical, kPdLowPhysical | 0x3ULL);
    fill_table_page(ram, kPdLowPhysical, 0, true);
    // PML4[511] -> high PDPT; PDPT[510] -> high PD; PD maps [0, 1 GiB).
    put_page(ram, kPml4Physical + (511ULL * 8U), kPdptHighPhysical | 0x3ULL);
    put_page(ram, kPdptHighPhysical + (510ULL * 8U), kPdHighPhysical | 0x3ULL);
    fill_table_page(ram, kPdHighPhysical, 0, true);

    // ---- E820: [0, 1 MiB) reserved, [1 MiB, RAM top) usable. ----
    const uint64_t usable_base = kLowMegabyte;
    const uint64_t usable_size = ram.size() - kLowMegabyte;
    put_u32(ram, kBootInfoPhysical, 0x00114514U); // magic
    put_u32(ram, kBootInfoPhysical + 4, 1);       // version
    put_u32(ram, kBootInfoPhysical + 8, static_cast<uint32_t>(kBootInfoBytes));
    put_u32(ram, kBootInfoPhysical + 12, 2); // e820_count
    // Entry 0: reserved low megabyte.
    put_u64(ram, kBootInfoPhysical + 16, 0);
    put_u64(ram, kBootInfoPhysical + 24, kLowMegabyte);
    put_u32(ram, kBootInfoPhysical + 32, 2);
    // Entry 1: usable remainder.
    put_u64(ram, kBootInfoPhysical + 36, usable_base);
    put_u64(ram, kBootInfoPhysical + 44, usable_size);
    put_u32(ram, kBootInfoPhysical + 52, 1);
    // Framebuffer: the LFB reservation rides the RAM tail (spec 5.4);
    // geometry 640x480x32 until the display batch pins the final mode.
    const uint64_t framebuffer_at = kBootInfoPhysical + 16 + (128ULL * 20U);
    std::array<uint8_t, kFramebufferBytes> framebuffer{};
    const uint64_t lfb_physical = ram.size() - 0x400000ULL; // ram_size + 4 MiB window
    constexpr uint32_t width = 640;
    constexpr uint32_t height = 480;
    constexpr uint32_t bpp = 32;
    const auto put_field = [&](uint64_t at, uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) {
            framebuffer[at + i] = static_cast<uint8_t>(value >> (8U * i));
        }
    };
    put_field(0, static_cast<uint32_t>(lfb_physical));
    put_field(8, width * (bpp / 8U)); // pitch
    put_field(12, width);
    put_field(16, height);
    put_field(20, bpp);
    put_field(24, 8);  // red size
    put_field(28, 16); // red shift
    put_field(32, 8);  // green size
    put_field(36, 8);  // green shift
    put_field(40, 8);  // blue size
    put_field(44, 0);  // blue shift
    (void)ram.copy_in(framebuffer_at, framebuffer);
    // Kernel facts.
    put_u64(ram, framebuffer_at + kFramebufferBytes, header.load_paddr);
    put_u64(ram, framebuffer_at + kFramebufferBytes + 8, header.file_size);
    put_u64(ram, framebuffer_at + kFramebufferBytes + 16, header.mem_size);
    put_u64(ram, framebuffer_at + kFramebufferBytes + 24, header.entry);

    // ---- ACPI: the kernel's finder scans the low window (0xE0000). ----
    (void)micro_forge::kernel::build_acpi_tables(ram, micro_forge::kernel::AcpiLayout{});

    // ---- CPU handoff state (what LmEntry leaves behind). ----
    cpu.set_control_register(2, kPml4Physical); // CR3
    cpu.set_paging(true);
    cpu.set_control_register(0, 0x80000001ULL); // CR0.PG | PE
    cpu.set_control_register(3, 0x20ULL);       // CR4.PAE
    (void)cpu.write_msr(0xC0000080U, 0x500ULL); // EFER.LME | LMA
    cpu.set_gdtr(0x8000, 0x17);                 // nominal 64-bit GDT
    // JIT may only compile the file-backed extent: everything past
    // load_paddr + file_size is data/bss, where a misparsed block would
    // happily run garbage.
    cpu.set_code_range(kHighHalfBase + header.load_paddr,
                       kHighHalfBase + header.load_paddr + header.file_size);
    cpu.set_rip(kHighHalfBase + header.entry);
    cpu.set_gpr(micro_forge::arch::x86_64::Reg::kRsp, kBootStackTop);
    cpu.set_gpr(micro_forge::arch::x86_64::Reg::kRdi, kHighHalfBase + kBootInfoPhysical);
    return {};
}

} // namespace micro_forge::loader
