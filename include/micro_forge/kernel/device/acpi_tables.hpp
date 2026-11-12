/**
 * @file acpi_tables.hpp
 * @brief ACPI table builder: RSDP, RSDT and MADT (batch 6c).
 * @version 0.1.0
 *
 * The boot shim lays out what the kernel's ACPI walker expects: an
 * RSDP in the low search window (0xE0000), an RSDT pointing at a MADT
 * whose entries name one boot processor, the IO-APIC at its pc-machine
 * address, and the ISA IRQ0->GSI2 override. All checksums are computed,
 * not pasted.
 */

#pragma once

#include <cstdint>

#include "micro_forge/kernel/memory/ram_region.hpp"

namespace micro_forge::kernel {

/// Guest-physical placement of the ACPI tables.
struct AcpiLayout {
    /// RSDP (root pointer) address inside the low search window.
    uint64_t rsdp = 0xE0000;
    /// RSDT (table directory) address.
    uint64_t rsdt = 0xE1000;
    /// MADT (interrupt topology) address.
    uint64_t madt = 0xE2000;
    /// Local APIC MMIO base the MADT reports.
    uint64_t lapic_base = 0xFEE00000;
    /// IO-APIC MMIO base the MADT reports.
    uint64_t ioapic_base = 0xFEC00000;
};

/**
 * @brief Writes the three tables into guest RAM.
 *
 * @param[in] ram Guest RAM region (the tables live in its low window).
 * @param[in] layout Placement and reported addresses.
 * @return True when every write fit; false otherwise.
 */
bool build_acpi_tables(RamRegion& ram, const AcpiLayout& layout);

} // namespace micro_forge::kernel
