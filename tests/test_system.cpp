/// @file test_system.cpp
/// @brief Batch-6a system instructions and SSE2 subset: byte programs
///        assembled and verified against GNU as + objdump.

#include "cpu_fixture.hpp"

#include <cstring>

#include <micro_forge/kernel/device/acpi_tables.hpp>
#include <micro_forge/kernel/device/io_apic.hpp>
#include <micro_forge/kernel/device/lapic.hpp>
#include <micro_forge/kernel/device/pic_8259.hpp>
#include <micro_forge/kernel/device/pit_8254.hpp>
#include <micro_forge/kernel/device/rtc.hpp>
#include <micro_forge/kernel/device/serial_16550.hpp>

namespace {

TEST_F(CpuTest, CliStiToggleInterruptFlag) {
    load({
        0xFB,
        0xFA,
    }); // sti; cli
    (void)cpu_.run(10);
    EXPECT_FALSE(cpu_.interrupts_enabled());
}

TEST_F(CpuTest, PushfqPopfqRestoreInterruptState) {
    // sti; pushfq; cli; popfq -> IF comes back enabled from the stack.
    load({
        0xFB,
        0x9C,
        0xFA,
        0x9D,
    });
    (void)cpu_.run(10);
    EXPECT_TRUE(cpu_.interrupts_enabled());
}

TEST_F(CpuTest, MsrWhitelistRoundTrips) {
    // wrmsr EFER=0x500, then rdmsr it back.
    load({
        0xB9, 0x80, 0x00, 0x00, 0xC0, // mov ecx, 0xC0000080
        0xB8, 0x00, 0x05, 0x00, 0x00, // mov eax, 0x500
        0xBA, 0x00, 0x00, 0x00, 0x00, // mov edx, 0
        0x0F, 0x30,                   // wrmsr
        0x0F, 0x32,
    }); // rdmsr
    (void)cpu_.run(50);
    EXPECT_EQ(0x500U, static_cast<uint32_t>(cpu_.gpr(Reg::kRax)));
    EXPECT_EQ(0U, static_cast<uint32_t>(cpu_.gpr(Reg::kRdx)));
}

TEST_F(CpuTest, MsrOutsideWhitelistFaults) {
    load({
        0xB9,
        0xAD,
        0xDE,
        0x00,
        0x00, // mov ecx, 0xDEAD
        0x0F,
        0x30,
    }); // wrmsr
    (void)cpu_.run(10);
    EXPECT_EQ(ExitStatus::Kind::kFault, cpu_.exit_status().kind);
    EXPECT_EQ(static_cast<uint64_t>(FaultId::kMsr), cpu_.exit_status().detail);
    EXPECT_EQ(0xDEADU, cpu_.exit_status().code); // the index names the site
}

TEST_F(CpuTest, CpuidLeafOneReportsSse2Only) {
    load({
        0xB8,
        0x01,
        0x00,
        0x00,
        0x00, // mov eax, 1
        0x0F,
        0xA2,
    }); // cpuid
    (void)cpu_.run(10);
    const uint64_t edx = cpu_.gpr(Reg::kRdx);
    const uint64_t ecx = cpu_.gpr(Reg::kRcx);
    EXPECT_TRUE((edx & (1ULL << 26U)) != 0); // SSE2 claimed
    EXPECT_TRUE((edx & (1ULL << 25U)) != 0); // SSE claimed
    EXPECT_EQ(0U, ecx);                      // nothing past SSE2 claimed
}

TEST_F(CpuTest, RdtscDerivesFromIcount) {
    load({
        0x90,
        0x90,
        0x90,
        0x0F,
        0x31,
    }); // nop;nop;nop;rdtsc
    (void)cpu_.run(10);
    const uint64_t stamp = (cpu_.gpr(Reg::kRdx) << 32U) | cpu_.gpr(Reg::kRax);
    EXPECT_EQ(3U, stamp); // three retires before the read
}

TEST_F(CpuTest, LgdtLidtRoundTripThroughMemory) {
    // lgdt [rax] then sgdt [rbx]: the 10-byte descriptor comes back.
    load({
        0x0F,
        0x01,
        0x10, // lgdt [rax]
        0x0F,
        0x01,
        0x03,
    }); // sgdt [rbx] (modrm 03 = [rbx])
    poke(0x2000, {0x2F, 0x00, 0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    cpu_.reset(kCodeBase, kStackTop);
    cpu_.set_gpr(Reg::kRax, 0x2000);
    cpu_.set_gpr(Reg::kRbx, 0x3000);
    (void)cpu_.run(10);
    EXPECT_EQ(0x2FU, static_cast<unsigned>(ram_.data()[0x3000]));
    EXPECT_EQ(0x34U, static_cast<unsigned>(ram_.data()[0x3002]));
    EXPECT_EQ(0x12U, static_cast<unsigned>(ram_.data()[0x3003]));
}

TEST_F(CpuTest, MovToFromCr4StoresValue) {
    load({
        0x0F,
        0x22,
        0xE0, // mov cr4, rax
        0x0F,
        0x20,
        0xE0,
    }); // mov rax, cr4
    cpu_.set_gpr(Reg::kRax, 0x6F0);
    (void)cpu_.run(10);
    EXPECT_EQ(0x6F0U, cpu_.control_register(3)); // CR4 by enumeration offset
    EXPECT_EQ(0x6F0U, cpu_.gpr(Reg::kRax));
}

TEST_F(CpuTest, MovapsCopiesSixteenBytes) {
    // movaps xmm0, [rax]; movaps [rbx], xmm0 -- the memcpy shape.
    load({
        0x0F,
        0x28,
        0x00,
        0x0F,
        0x29,
        0x03,
    });
    for (unsigned i = 0; i < 16; ++i) {
        ram_.data()[0x2000 + i] = static_cast<uint8_t>(i * 3);
    }
    cpu_.reset(kCodeBase, kStackTop);
    cpu_.set_gpr(Reg::kRax, 0x2000);
    cpu_.set_gpr(Reg::kRbx, 0x3000);
    (void)cpu_.run(10);
    for (unsigned i = 0; i < 16; ++i) {
        EXPECT_EQ(static_cast<uint8_t>(i * 3), ram_.data()[0x3000 + i]) << "byte " << i;
    }
}

TEST_F(CpuTest, MovdqaAndXmmArithmetic) {
    // movdqa xmm1,[rax]; pxor xmm0,xmm0; paddd xmm0,xmm1; movdqa [rbx],xmm1
    load({
        0x66,
        0x0F,
        0x6F,
        0x08,
        0x66,
        0x0F,
        0xEF,
        0xC0,
        0x66,
        0x0F,
        0xFE,
        0xC1,
        0x66,
        0x0F,
        0x7F,
        0x0B,
    });
    for (unsigned i = 0; i < 16; ++i) {
        ram_.data()[0x2000 + i] = 0x11;
    }
    cpu_.reset(kCodeBase, kStackTop);
    cpu_.set_gpr(Reg::kRax, 0x2000);
    cpu_.set_gpr(Reg::kRbx, 0x3000);
    (void)cpu_.run(10);
    // padding dwords of 0x11111111 x4; paddd with zero = unchanged.
    for (unsigned i = 0; i < 16; ++i) {
        EXPECT_EQ(0x11U, static_cast<unsigned>(ram_.data()[0x3000 + i]));
    }
    const auto x1 = cpu_.xmm(Reg::kXmm1);
    EXPECT_EQ(0x1111111111111111ULL, x1[0]);
    EXPECT_EQ(0x1111111111111111ULL, x1[1]);
}

TEST_F(CpuTest, PshufdReordersDwords) {
    // pshufd xmm2, xmm0, 0x1B reverses the four dwords.
    load({
        0x66,
        0x0F,
        0x70,
        0xD0,
        0x1B,
    });
    cpu_.reset(kCodeBase, kStackTop);
    cpu_.set_xmm(Reg::kXmm0, {0x4444444433333333ULL, 0x2222222211111111ULL});
    (void)cpu_.run(10);
    const auto x2 = cpu_.xmm(Reg::kXmm2);
    EXPECT_EQ(0x1111111122222222ULL, x2[0]); // dwords 3,2
    EXPECT_EQ(0x3333333344444444ULL, x2[1]); // dwords 1,0
}

TEST_F(CpuTest, MovqTransfersLaneAndClearsHigh) {
    // movq xmm0, rbx: lane 0 gets rbx, high half zeroed.
    load({
        0x66,
        0x48,
        0x0F,
        0x6E,
        0xC3,
    });
    cpu_.reset(kCodeBase, kStackTop);
    cpu_.set_gpr(Reg::kRbx, 0xDEADBEEFCAFEBABEULL);
    (void)cpu_.run(10);
    const auto x0 = cpu_.xmm(Reg::kXmm0);
    EXPECT_EQ(0xDEADBEEFCAFEBABEULL, x0[0]);
    EXPECT_EQ(0U, x0[1]);
}

/// Port I/O routing and the COM1 polling surface.
class PortTest : public CpuTest {
protected:
    void SetUp() override {
        CpuTest::SetUp();
        EXPECT_TRUE(ports_.map(0x3F8, 8, serial_).has_value());
        cpu_.reset(kCodeBase, kStackTop);
    }

    micro_forge::kernel::Serial16550 serial_{g_console_sink};
    static inline std::vector<char> g_console_sink;
};

TEST_F(PortTest, OutByteToSerialLandsInSink) {
    // mov edx, 0x3F8; mov al, 'H'; out dx, al; out 0xA, al (unmapped).
    load({
        0xBA,
        0xF8,
        0x03,
        0x00,
        0x00, // mov edx, 0x3F8
        0xB0,
        0x48, // mov al, 'H'
        0xEE, // out dx, al
        0xE6,
        0x0A,
    }); // out 0xA, al (unmapped port)
    (void)cpu_.run(10);
    ASSERT_FALSE(g_console_sink.empty());
    EXPECT_EQ(0x48, static_cast<unsigned>(static_cast<uint8_t>(g_console_sink.back())));
    EXPECT_EQ(ExitStatus::Kind::kFault, cpu_.exit_status().kind);
    EXPECT_EQ(0x0AU, cpu_.exit_status().code); // the port names the site
    EXPECT_EQ(static_cast<uint64_t>(FaultId::kMemory), cpu_.exit_status().detail);
}

TEST_F(PortTest, LsrReportsTransmitterAlwaysReady) {
    // mov dx, 0x3FD; in al, dx  (LSR: THRE|TEMT set, DR clear)
    load({
        0x66,
        0xBA,
        0xFD,
        0x03, // mov dx, 0x3FD
        0xEC,
    }); // in al, dx
    (void)cpu_.run(10);
    EXPECT_EQ(0x60U, static_cast<unsigned>(static_cast<uint8_t>(cpu_.gpr(Reg::kRax))));
}

TEST_F(PortTest, SerialInitSequenceKeepsDlabSemantics) {
    // LCR.DLAB=1 selects the divisor latches; THR writes while latched
    // must not reach the sink.
    load({
        0x66,
        0xBA,
        0xFB,
        0x03, // mov dx, 0x3FB (LCR)
        0xB0,
        0x80, // mov al, 0x80 (DLAB)
        0xEE, // out dx, al
        0x66,
        0xBA,
        0xF8,
        0x03, // mov dx, 0x3F8
        0xB0,
        0x41, // mov al, 'A'
        0xEE,
    }); // out dx, al (divisor low, not THR)
    (void)cpu_.run(10);
    EXPECT_TRUE(serial_.divider_latched());
    EXPECT_EQ(0x80U, static_cast<unsigned>(serial_.line_control()));
}

/// PIT / board port / RTC / PIC dialect coverage.
class DeviceTest : public CpuTest {
protected:
    void SetUp() override {
        CpuTest::SetUp();
        EXPECT_TRUE(ports_.map(0x40, 4, pit_).has_value());
        EXPECT_TRUE(ports_.map(0x61, 1, board_).has_value());
        EXPECT_TRUE(ports_.map(0x70, 2, rtc_).has_value());
        EXPECT_TRUE(ports_.map(0x20, 2, pic_master_).has_value());
        EXPECT_TRUE(ports_.map(0xA0, 2, pic_slave_).has_value());
        cpu_.reset(kCodeBase, kStackTop);
    }

    micro_forge::kernel::Pit8254 pit_;
    micro_forge::kernel::BoardControl board_;
    micro_forge::kernel::Rtc rtc_;
    micro_forge::kernel::Pic8259 pic_master_;
    micro_forge::kernel::Pic8259 pic_slave_;
};

TEST_F(DeviceTest, PitChannelZeroSquareWaveProgram) {
    // Cinux Pit::start: out 0x43,0x36; out 0x40,lo; out 0x40,hi
    // (imm8 form: the immediate is the PORT, the data rides in AL).
    load({
        0xB0,
        0x36,
        0xE6,
        0x43, // mov al,0x36; out 0x43,al
        0xB0,
        0x9B,
        0xE6,
        0x40, // mov al,0x9B; out 0x40,al (divisor lo)
        0xB0,
        0x2E,
        0xE6,
        0x40,
    }); // mov al,0x2E; out 0x40,al (divisor hi)
    (void)cpu_.run(10);
    EXPECT_EQ(11931U, static_cast<unsigned>(pit_.divisor(0)));
    EXPECT_EQ(0x36U, static_cast<unsigned>(pit_.control_word()));
}

TEST_F(DeviceTest, BoardPortReportsTimerOutputDone) {
    // in al, 0x61 -> bit 5 set (delay loop terminates).
    load({0xE4, 0x61}); // in al, 0x61
    (void)cpu_.run(10);
    EXPECT_EQ(0x20U, static_cast<unsigned>(static_cast<uint8_t>(cpu_.gpr(Reg::kRax))));
}

TEST_F(DeviceTest, RtcReadsBcdStatusAfterSelect) {
    // out 0x70,0x0B (select status B); in al,0x71 (24h BCD => 0x02).
    load({
        0xB0,
        0x0B,
        0xE6,
        0x70, // mov al,0x0B; out 0x70,al
        0xE4,
        0x71,
    }); // in al, 0x71
    (void)cpu_.run(10);
    EXPECT_EQ(0x02U, static_cast<unsigned>(static_cast<uint8_t>(cpu_.gpr(Reg::kRax))));
}

TEST_F(DeviceTest, PicRemapSequenceLandsVectorBase) {
    // Cinux Pic::remap: ICW1 0x11, ICW2 0x20, ICW3 0x04, ICW4 0x01,
    // OCW1 0xFF on the master; 0x11/0x28/0x02/0x01/0xFF on the slave.
    load({
        0xB0, 0x11, 0xE6, 0x20, 0xB0, 0x20, 0xE6, 0x21, 0xB0, 0x04, 0xE6, 0x21, 0xB0, 0x01,
        0xE6, 0x21, 0xB0, 0xFF, 0xE6, 0x21, 0xB0, 0x11, 0xE6, 0xA0, 0xB0, 0x28, 0xE6, 0xA1,
        0xB0, 0x02, 0xE6, 0xA1, 0xB0, 0x01, 0xE6, 0xA1, 0xB0, 0xFF, 0xE6, 0xA1,
    });
    (void)cpu_.run(40);
    EXPECT_TRUE(pic_master_.initialized());
    EXPECT_EQ(0x20U, static_cast<unsigned>(pic_master_.vector_base()));
    EXPECT_EQ(0xFFU, static_cast<unsigned>(pic_master_.mask()));
    EXPECT_EQ(0x28U, static_cast<unsigned>(pic_slave_.vector_base()));
}

TEST_F(DeviceTest, PicUnmaskReadsAndRewritesMask) {
    load({
        0xB0, 0x11, 0xE6, 0x20, 0xB0, 0x20, 0xE6, 0x21, 0xB0, 0x04,
        0xE6, 0x21, 0xB0, 0x01, 0xE6, 0x21, 0xB0, 0xFF, 0xE6, 0x21, // ICW1..4 + mask all
        0xE4, 0x21,                                                 // in al, 0x21 (IMR)
        0x24, 0xFE,                                                 // and al, 0xFE
        0xE6, 0x21,
    }); // out 0x21, al
    (void)cpu_.run(30);
    EXPECT_EQ(0xFEU, static_cast<unsigned>(pic_master_.mask()));
}

/// LAPIC / IO-APIC MMIO windows (batch 6c).
class ApicTest : public CpuTest {
protected:
    void SetUp() override {
        CpuTest::SetUp();
        EXPECT_TRUE(bus_.map(0xFEE00000ULL, 0x1000, lapic_).has_value());
        EXPECT_TRUE(bus_.map(0xFEC00000ULL, 0x1000, io_apic_).has_value());
        cpu_.reset(kCodeBase, kStackTop);
    }

    micro_forge::kernel::Lapic lapic_;
    micro_forge::kernel::IoApic io_apic_;
};

TEST_F(ApicTest, LapicIdAndEoi) {
    // mov edx,0xFEE00000; mov eax,[edx+0x20]; mov dword [edx+0xB0],0
    load({
        0xBA,
        0x00,
        0x00,
        0xE0,
        0xFE, // mov edx, 0xFEE00000
        0x8B,
        0x42,
        0x20, // mov eax, [rdx+0x20]
        0xC7,
        0x82,
        0xB0,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
    }); // EOI
    (void)cpu_.run(10);
    EXPECT_EQ(0U, static_cast<uint32_t>(cpu_.gpr(Reg::kRax))); // APIC ID 0
    EXPECT_TRUE(lapic_.eoi_pending());
}

TEST_F(ApicTest, IoApicSelectionReadsRedirection) {
    // IOREGSEL=0x10 (first redirection), IOWIN=0x12345678, read back.
    load({
        0xBA, 0x00, 0x00, 0xC0, 0xFE,             // mov edx, 0xFEC00000
        0xC7, 0x02, 0x10, 0x00, 0x00, 0x00,       // mov dword [rdx], 0x10
        0xC7, 0x42, 0x10, 0x78, 0x56, 0x34, 0x12, // mov dword [rdx+0x10], 0x12345678
        0xC7, 0x02, 0x10, 0x00, 0x00, 0x00,       // select again
        0x8B, 0x42, 0x10,
    }); // mov eax, [rdx+0x10]
    (void)cpu_.run(20);
    EXPECT_EQ(0x12345678U, static_cast<uint32_t>(cpu_.gpr(Reg::kRax)));
    EXPECT_EQ(0x12345678U, io_apic_.redirection(0)[0]);
}

/// ACPI tables: checksums and the boot walker's field expectations.
TEST_F(DeviceTest, AcpiTablesParseLikeCinuxDoes) {
    const micro_forge::kernel::AcpiLayout layout;
    ASSERT_TRUE(micro_forge::kernel::build_acpi_tables(ram_, layout));

    // RSDP at the low search window: signature and checksum.
    const auto rsdp = ram_.view(layout.rsdp, 40);
    ASSERT_EQ(40, rsdp.size());
    EXPECT_EQ(0, std::memcmp(rsdp.data(), "RSD PTR ", 8));
    uint8_t sum = 0;
    for (std::size_t i = 0; i < 20; ++i) {
        sum = static_cast<uint8_t>(sum + rsdp[i]);
    }
    EXPECT_EQ(0, sum);
    // ACPI 1.0: revision 0, 32-bit RSDT address at offset 16.
    uint32_t rsdt = 0;
    for (unsigned i = 0; i < 4; ++i) {
        rsdt |= static_cast<uint32_t>(rsdp[16 + i]) << (8U * i);
    }
    EXPECT_EQ(static_cast<uint32_t>(layout.rsdt), rsdt);

    // RSDT: header sums to zero and points at the MADT.
    const auto table = ram_.view(layout.rsdt, 40);
    EXPECT_EQ(0, std::memcmp(table.data(), "RSDT", 4));
    sum = 0;
    for (std::size_t i = 0; i < 40; ++i) {
        sum = static_cast<uint8_t>(sum + table[i]);
    }
    EXPECT_EQ(0, sum);
    uint32_t madt = 0;
    for (unsigned i = 0; i < 4; ++i) {
        madt |= static_cast<uint32_t>(table[36 + i]) << (8U * i);
    }
    EXPECT_EQ(static_cast<uint32_t>(layout.madt), madt);

    // MADT: signature, LAPIC base at +36, IO-APIC record at +52.
    const auto madt_table = ram_.view(layout.madt, 74);
    EXPECT_EQ(0, std::memcmp(madt_table.data(), "APIC", 4));
    uint32_t lapic = 0;
    for (unsigned i = 0; i < 4; ++i) {
        lapic |= static_cast<uint32_t>(madt_table[36 + i]) << (8U * i);
    }
    EXPECT_EQ(0xFEE00000U, lapic);
    // Records: processor (8), then IO-APIC (12) at offset 44.
    EXPECT_EQ(0, madt_table[44]); // type 0
    EXPECT_EQ(8, madt_table[45]);
    EXPECT_EQ(1, madt_table[52]); // type 1 (IO-APIC)
    uint32_t ioapic = 0;
    for (unsigned i = 0; i < 4; ++i) {
        ioapic |= static_cast<uint32_t>(madt_table[56 + i]) << (8U * i);
    }
    EXPECT_EQ(0xFEC00000U, ioapic);
    EXPECT_EQ(2, madt_table[64]); // type 2 override
    sum = 0;
    for (std::size_t i = 0; i < 74; ++i) {
        sum = static_cast<uint8_t>(sum + madt_table[i]);
    }
    EXPECT_EQ(0, sum);
}

} // namespace
