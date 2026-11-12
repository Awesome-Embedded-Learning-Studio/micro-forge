/**
 * @file acpi_tables.cpp
 * @brief Table bytes and checksums (batch 6c).
 * @version 0.1.0
 *
 * Layouts mirror the wire structures Cinux parses: Rsdp (40 bytes, the
 * 1.0 subset with a 32-bit RSDT address), TableHeader (36 bytes) plus
 * a 32-bit entry array, and the MADT body (LAPIC base + flags + three
 * records: processor, IO-APIC, IRQ0 override).
 */

#include "micro_forge/kernel/device/acpi_tables.hpp"

#include <array>
#include <cstring>

#include "micro_forge/kernel/le_access.hpp"

namespace micro_forge::kernel {

namespace {

constexpr uint64_t kHeaderBytes = 36;
constexpr uint64_t kRsdpBytes = 40;

void put_bytes(RamRegion& ram, uint64_t address, const void* data, std::size_t size) {
    (void)ram.copy_in(address, {static_cast<const uint8_t*>(data), size});
}

/// Folds every byte to zero modulo 256 (ACPI checksum rule).
void stamp_checksum(uint8_t* table, std::size_t size) {
    table[9] = 0; // checksum byte of a TableHeader
    uint8_t sum = 0;
    for (std::size_t i = 0; i < size; ++i) {
        sum = static_cast<uint8_t>(sum + table[i]);
    }
    table[9] = static_cast<uint8_t>(0U - sum);
}

/// Fills a TableHeader at @p table (signature, length, revision, OEM).
void put_header(uint8_t* table, const char (&signature)[5], uint32_t length) {
    for (unsigned i = 0; i < 4; ++i) {
        table[i] = static_cast<uint8_t>(signature[i]);
    }
    put_le32(table + 4, length);
    table[8] = 1; // revision
    for (unsigned i = 0; i < 6; ++i) {
        table[10 + i] = static_cast<uint8_t>("MFORGE"[i]);
    }
    for (unsigned i = 0; i < 8; ++i) {
        table[16 + i] = static_cast<uint8_t>("MICROFRG"[i]);
    }
    put_le32(table + 24, 1);
    for (unsigned i = 0; i < 4; ++i) {
        table[28 + i] = static_cast<uint8_t>("FCOR"[i]);
    }
    put_le32(table + 32, 1);
}

} // namespace

bool build_acpi_tables(RamRegion& ram, const AcpiLayout& layout) {
    // ---- MADT: header + LAPIC/flags + processor + IO-APIC + override.
    {
        constexpr auto madt_bytes = static_cast<uint32_t>(kHeaderBytes + 8 + 8 + 12 + 10);
        std::array<uint8_t, 74> table{};
        put_header(table.data(), "APIC", madt_bytes);
        put_le32(table.data() + kHeaderBytes, static_cast<uint32_t>(layout.lapic_base));
        put_le32(table.data() + kHeaderBytes + 4, 1); // PCAT-compatible
        uint8_t* entry = table.data() + kHeaderBytes + 8;
        // Type 0: processor (acpi id 0, apic id 0, enabled+online).
        entry[0] = 0;
        entry[1] = 8;
        entry[2] = 0;
        entry[3] = 0;
        put_le32(entry + 4, 1);
        entry += 8;
        // Type 1: IO-APIC (id 0, address, GSI base 0).
        entry[0] = 1;
        entry[1] = 12;
        entry[2] = 0;
        entry[3] = 0;
        put_le32(entry + 4, static_cast<uint32_t>(layout.ioapic_base));
        put_le32(entry + 8, 0);
        entry += 12;
        // Type 2: IRQ0 -> GSI2 (the pc-machine override).
        entry[0] = 2;
        entry[1] = 10;
        entry[2] = 0; // bus 0 (ISA)
        entry[3] = 0; // source IRQ0
        put_le32(entry + 4, 2);
        put_le16(entry + 8, 0);
        stamp_checksum(table.data(), madt_bytes);
        put_bytes(ram, layout.madt, table.data(), madt_bytes);
    }

    // ---- RSDT: one 32-bit pointer to the MADT.
    {
        constexpr auto rsdt_bytes = static_cast<uint32_t>(kHeaderBytes + 4);
        std::array<uint8_t, rsdt_bytes> table{};
        put_header(table.data(), "RSDT", rsdt_bytes);
        put_le32(table.data() + kHeaderBytes, static_cast<uint32_t>(layout.madt));
        stamp_checksum(table.data(), rsdt_bytes);
        put_bytes(ram, layout.rsdt, table.data(), rsdt_bytes);
    }

    // ---- RSDP: ACPI 1.0 shape (revision 0, 32-bit RSDT address).
    {
        std::array<uint8_t, kRsdpBytes> table{};
        std::memcpy(table.data(), "RSD PTR ", 8);
        std::memcpy(table.data() + 9, "MFORGE", 6);
        table[15] = 0; // revision 0
        put_le32(table.data() + 16, static_cast<uint32_t>(layout.rsdt));
        put_le32(table.data() + 20, static_cast<uint32_t>(kRsdpBytes));
        put_le64(table.data() + 24, layout.rsdt); // xsdt unused at revision 0
        // RSDP checksum: first 20 bytes sum to zero.
        uint8_t sum = 0;
        for (std::size_t i = 0; i < 20; ++i) {
            sum = static_cast<uint8_t>(sum + table[i]);
        }
        table[8] = static_cast<uint8_t>(0U - sum);
        put_bytes(ram, layout.rsdp, table.data(), kRsdpBytes);
    }
    return true;
}

} // namespace micro_forge::kernel
