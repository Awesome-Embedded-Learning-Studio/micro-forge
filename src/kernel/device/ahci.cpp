/**
 * @file ahci.cpp
 * @brief AHCI command execution over a host disk image (batch 6d).
 * @version 0.1.0
 *
 * Command list layout (AHCI 1.3): one 32-byte header at CLB containing
 * the command-table base; the table starts with the register H2D FIS
 * (type 0x27) and the PRDT follows at offset 128. Each PRDT entry is 16
 * bytes: data base, upper, reserved, flags (byte count in the low 22
 * bits). One region per command is what the driver builds.
 */

#include "micro_forge/kernel/device/ahci.hpp"

#include <array>
#include <cstring>

#include "micro_forge/kernel/le_access.hpp"

namespace micro_forge::kernel {

namespace {
constexpr uint8_t kFisType = 0x27;
constexpr uint8_t kFisFlagCommand = 0x80;
constexpr uint8_t kAtaReadDmaExt = 0x25;
constexpr uint8_t kAtaWriteDmaExt = 0x35;
constexpr uint8_t kAtaIdentify = 0xEC;
constexpr uint8_t kAtaFlushCacheExt = 0xEA;
constexpr uint64_t kSectorBytes = 512;
constexpr uint64_t kCommandTableOffset = 0x80;   // PRDT starts here in the table
constexpr uint32_t kTaskFileReady = 0x50U;       // RDY | DRDY
constexpr uint32_t kPortInterruptDi = 1U << 1U;  // device-to-host FIS
constexpr uint32_t kPortInterruptPio = 1U << 2U; // PIO setup FIS
} // namespace

Ahci::Ahci(Bus& bus, std::vector<uint8_t>& disk, std::function<void()> interrupt)
    : RegisterRegionBase(window_.data(), window_.size()), bus_(bus), disk_(disk),
      interrupt_(std::move(interrupt)) {
    prime();
}

void Ahci::prime() {
    // Called after construction when the window is bound: write the
    // controller identity into the register image.
    put_le32(window() + kCapabilities, 0x30011F0FU); // NCQ, 64-bit, AHCI only
    put_le32(window() + kPortsImplemented, 0x1U);    // port 0 only
    put_le32(window() + kVersion, 0x00010000U);      // VS 1.0
    put_le32(window() + static_cast<uint64_t>(kPortBase) + kPortSignature, 0x00000101U); // SATA
    put_le32(window() + static_cast<uint64_t>(kPortBase) + kPortSataStatus,
             0x00000123U); // present+ready
    put_le32(window() + static_cast<uint64_t>(kPortBase) + kPortTaskFile, kTaskFileReady);
}

void Ahci::on_write(uint64_t offset, unsigned width, uint64_t value) {
    if (width != 4) {
        return;
    }
    if (offset == static_cast<uint64_t>(kPortBase) + kPortInterruptStatus) {
        port_interrupt_ &= ~static_cast<uint32_t>(value); // write-one-to-clear
        return;
    }
    if (offset == static_cast<uint64_t>(kPortBase) + kPortCommand) {
        // ST/FR are the switches; CR/FR(state) mirror them. Drop the
        // read-only state bits a read-modify-write hands back, then
        // derive them from the switches.
        const auto requested = static_cast<uint32_t>(value) & ~0xC000U;
        uint32_t state = requested;
        if ((requested & 0x1U) != 0U) {
            state |= 0x8000U; // CR: command list running
        }
        if ((requested & 0x10U) != 0U) {
            state |= 0x4000U; // FR: FIS receive running
        }
        put_le32(window() + static_cast<uint64_t>(kPortBase) + kPortCommand, state);
        return;
    }
    if (offset == static_cast<uint64_t>(kPortBase) + kPortCommandIssue) {
        const auto issue = static_cast<uint32_t>(value);
        if ((issue & 0x1U) != 0U) {
            service();
        }
        return;
    }
    if (offset == kGlobalControl) {
        // HR (bit 0) is self-clearing; every other bit (AHCI_ENABLE
        // above all) must stay set or the driver polls forever.
        const auto requested = static_cast<uint32_t>(value);
        put_le32(window() + kGlobalControl, requested & ~0x1U);
    }
}

bool Ahci::decode(Command& out) const {
    const uint64_t clb =
        (static_cast<uint64_t>(
            get_le32(window() + static_cast<uint64_t>(kPortBase) + kCommandListBase))) |
        (static_cast<uint64_t>(
             get_le32(window() + static_cast<uint64_t>(kPortBase) + kCommandListBaseUpper))
         << 32U);
    if (clb == 0) {
        return false;
    }
    // Header: [0]=flags, [4]=prd_byte_count, [8]=command table base.
    const auto table_lo = bus_.read_u32(clb + 8U);
    const auto table_hi = bus_.read_u32(clb + 12U);
    if (!table_lo.has_value()) {
        return false;
    }
    const uint64_t table = static_cast<uint64_t>(*table_lo) |
                           (static_cast<uint64_t>(table_hi.has_value() ? *table_hi : 0) << 32U);
    // FIS at the table base: type, flags, command, feature, lba0-2,
    // device, lba3-5, feature_hi, count_lo, count_hi.
    const auto fis_command = bus_.read_u8(table + 2U);
    if (!fis_command.has_value()) {
        return false;
    }
    const auto fis_type = bus_.read_u8(table + 0U);
    if (!fis_type.has_value() || *fis_type != kFisType) {
        return false;
    }
    out.header_physical = clb; // the HBA writes the transfer length back here
    out.fis_command = *fis_command;
    const auto read_lba_byte = [&](uint8_t index) -> uint8_t {
        const auto v = bus_.read_u8(table + 4U + index);
        return v.has_value() ? *v : 0;
    };
    // LBA bytes 0-2 at offsets 4-6, device at 7, LBA 3-5 at 8-10.
    out.lba = static_cast<uint64_t>(read_lba_byte(0)) |
              (static_cast<uint64_t>(read_lba_byte(1)) << 8U) |
              (static_cast<uint64_t>(read_lba_byte(2)) << 16U) |
              (static_cast<uint64_t>(read_lba_byte(4)) << 24U) |
              (static_cast<uint64_t>(read_lba_byte(5)) << 32U) |
              (static_cast<uint64_t>(read_lba_byte(6)) << 40U);
    const auto lo = bus_.read_u8(table + 12U);
    const auto hi = bus_.read_u8(table + 13U);
    out.count = static_cast<uint16_t>(static_cast<uint32_t>(lo.has_value() ? *lo : 0) |
                                      (static_cast<uint32_t>(hi.has_value() ? *hi : 0) << 8U));
    // PRDT at table + 0x80: data base (8 bytes), reserved (4), flags (4).
    const auto base_lo = bus_.read_u32(table + kCommandTableOffset);
    const auto base_hi = bus_.read_u32(table + kCommandTableOffset + 4U);
    const auto flags = bus_.read_u32(table + kCommandTableOffset + 12U);
    if (!base_lo.has_value() || !flags.has_value()) {
        return false;
    }
    out.data_physical = static_cast<uint64_t>(*base_lo) |
                        (static_cast<uint64_t>(base_hi.has_value() ? *base_hi : 0) << 32U);
    // The PRDT DBC field holds bytes-1 (262144 encodes as 0, which the
    // driver's one-sector commands never hit).
    const uint32_t dbc = *flags & 0x3FFFFFU;
    out.data_bytes = (dbc == 0U) ? 0x40000U : (dbc + 1U);
    return true;
}

void Ahci::run(const Command& command) {
    const auto notify = [&] {
        // The HBA reports the first PRDT entry's length in the command
        // header's PRDTL field on completion (AHCI 1.3 4.2.1).
        const auto written = bus_.write_u32(command.header_physical + 4U, command.data_bytes);
        ;
        (void)written;
        port_interrupt_ |= kPortInterruptDi;
        put_le32(window() + static_cast<uint64_t>(kPortBase) + kPortInterruptStatus,
                 port_interrupt_);
        put_le32(window() + static_cast<uint64_t>(kPortBase) + kPortCommandIssue, 0); // done
        if (interrupt_) {
            interrupt_();
        }
    };
    switch (command.fis_command) {
    case kAtaIdentify: {
        // 512-byte IDENTIFY response. Word layout (ATA-8): 0 general
        // config, 27-46 model string, 49 LBA support, 60-61 LBA28
        // capacity, 83 command sets (valid + LBA48), 100-103 LBA48
        // capacity.
        std::array<uint8_t, kSectorBytes> identify{};
        const auto at = [&](unsigned word) -> size_t { return static_cast<size_t>(word) * 2U; };
        put_le16(identify.data() + at(0), 0x0040U); // fixed device, no CFA
        const uint64_t sectors = disk_.size() / kSectorBytes;
        put_le16(identify.data() + at(6), 1);                               // sectors per track
        put_le16(identify.data() + at(49), 0x0200U);                        // LBA supported
        put_le16(identify.data() + at(83), 0x4400U);                        // valid + LBA48
        put_le16(identify.data() + at(86), 0x0400U);                        // LBA48 enabled
        put_le32(identify.data() + at(60), static_cast<uint32_t>(sectors)); // LBA28
        for (unsigned i = 0; i < 4; ++i) { // LBA48 ceiling words 100..103
            put_le16(identify.data() + at(100U + i),
                     static_cast<uint16_t>((sectors >> (16U * i)) & 0xFFFFU));
        }
        constexpr char model_str[] = "MICRO-FORGE AHCI";
        for (unsigned i = 0; i < 20; ++i) { // model words 27..46, hi byte first
            const auto pair = static_cast<size_t>(i) * 2U;
            const char hi = pair < sizeof(model_str) - 1U ? model_str[pair] : ' ';
            const char lo = (pair + 1U) < sizeof(model_str) - 1U ? model_str[pair + 1U] : ' ';
            put_le16(identify.data() + at(27U + i),
                     static_cast<uint16_t>(static_cast<uint32_t>(static_cast<uint8_t>(hi)) << 8U) |
                         static_cast<uint8_t>(lo));
        }
        const uint32_t bytes = std::min<uint32_t>(command.data_bytes, kSectorBytes);
        for (uint32_t i = 0; i < bytes; i += 4) {
            uint32_t word = 0;
            for (unsigned j = 0; j < 4 && i + j < bytes; ++j) {
                word |= static_cast<uint32_t>(identify[i + j]) << (8U * j);
            }
            const auto written = bus_.write_u32(command.data_physical + i, word);
            ;
            (void)written;
        }
        break;
    }
    case kAtaReadDmaExt:
    case kAtaWriteDmaExt: {
        const uint64_t offset = command.lba * kSectorBytes;
        const uint64_t bytes = static_cast<uint64_t>(command.count) * kSectorBytes;
        if (offset > disk_.size() || bytes > disk_.size() - offset) {
            ++refused_;
            break;
        }
        for (uint64_t i = 0; i < bytes; i += 4) {
            if (command.fis_command == kAtaReadDmaExt) {
                const uint32_t word = static_cast<uint32_t>(disk_[offset + i]) |
                                      (static_cast<uint32_t>(disk_[offset + i + 1]) << 8U) |
                                      (static_cast<uint32_t>(disk_[offset + i + 2]) << 16U) |
                                      (static_cast<uint32_t>(disk_[offset + i + 3]) << 24U);
                const auto written = bus_.write_u32(command.data_physical + i, word);
                ;
                (void)written;
            } else {
                const auto word = bus_.read_u32(command.data_physical + i);
                const uint32_t value = word.has_value() ? *word : 0;
                disk_[offset + i] = static_cast<uint8_t>(value);
                disk_[offset + i + 1] = static_cast<uint8_t>(value >> 8U);
                disk_[offset + i + 2] = static_cast<uint8_t>(value >> 16U);
                disk_[offset + i + 3] = static_cast<uint8_t>(value >> 24U);
            }
        }
        sectors_done_ += command.count;
        break;
    }
    case kAtaFlushCacheExt:
        break; // writes are immediate in this model
    default:
        ++refused_;
        break;
    }
    notify();
}

void Ahci::service() {
    Command command;
    if (!decode(command)) {
        ++refused_;
        return;
    }
    run(command);
}

} // namespace micro_forge::kernel
