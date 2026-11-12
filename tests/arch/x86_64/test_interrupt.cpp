/**
 * @file test_interrupt.cpp
 * @brief Batch-6c vector delivery: IDT gate lookup, five-qword frame,
 *        handler execution, iretq return.
 */

#include "cpu_fixture.hpp"

namespace {

constexpr uint64_t kIdt = 0x3000;
constexpr uint64_t kHandler = 0x102000;
constexpr uint64_t kCounter = 0x4000;

/// One 64-bit interrupt gate in the IDT image.
void install_gate(RamRegion& ram, uint8_t vector, uint64_t handler, uint8_t type_attr) {
    const uint64_t base = kIdt + (static_cast<uint64_t>(vector) * 16U);
    const uint64_t lo = (handler & 0xFFFFULL) | (0x08ULL << 16U) |
                        (static_cast<uint64_t>(type_attr) << 40U) |
                        (((handler >> 16U) & 0xFFFFULL) << 48U);
    const uint64_t hi = handler >> 32U;
    for (unsigned i = 0; i < 8; ++i) {
        ram.data()[base + i] = static_cast<uint8_t>(lo >> (8U * i));
        ram.data()[base + 8U + i] = static_cast<uint8_t>(hi >> (8U * i));
    }
}

/// The 10-byte IDTR image that lidt consumes.
void install_idtr(RamRegion& ram, uint64_t address) {
    const uint64_t limit = 0x100; // 16 gates
    ram.data()[address] = static_cast<uint8_t>(limit & 0xFFU);
    ram.data()[address + 1] = static_cast<uint8_t>(limit >> 8U);
    for (unsigned i = 0; i < 8; ++i) {
        ram.data()[address + 2 + i] = static_cast<uint8_t>(address >> (8U * i));
    }
}

class InterruptTest : public CpuTest {
protected:
    void SetUp() override {
        CpuTest::SetUp();
        install_gate(ram_, 0x20, kHandler, 0x8E); // present, DPL0, interrupt gate
    }

    void write_counter(uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) {
            ram_.data()[kCounter + i] = static_cast<uint8_t>(value >> (8U * i));
        }
    }

    uint32_t counter() const {
        const auto bytes = ram_.view(kCounter, 4);
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            value |= static_cast<uint32_t>(bytes[i]) << (8U * i);
        }
        return value;
    }
};

TEST_F(InterruptTest, VectorDeliveryRunsHandlerAndIretqReturns) {
    // Main: lidt [rax]; sti; nop; hlt (auto). Handler at kHandler:
    // inc dword [0x4000]; iretq.
    load({
        0x0F,
        0x01,
        0x18, // lidt [rax]
        0xFB, // sti
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
        0x90,
    });
    const uint64_t handler_code[] = {0xFF, 0x04, 0x25, 0x00, 0x40, 0x00, 0x00, 0x48, 0xCF};
    for (unsigned i = 0; i < 9; ++i) {
        ram_.data()[kHandler + i] = static_cast<uint8_t>(handler_code[i]);
    }
    install_idtr(ram_, kIdt);              // the IDTR image lives at kIdt itself
    cpu_.set_gpr(Reg::kRax, kIdt + 0x100); // descriptor at idt+0x100
    for (unsigned i = 0; i < 10; ++i) {
        ram_.data()[kIdt + 0x100 + i] = 0;
    }
    // Put the 10-byte IDTR at kIdt+0x100 pointing at the gate table;
    // the limit must cover vector 0x20 (offset 0x200).
    const uint64_t descriptor = kIdt + 0x100;
    const uint64_t limit = 0x20F;
    ram_.data()[descriptor] = static_cast<uint8_t>(limit & 0xFFU);
    ram_.data()[descriptor + 1] = static_cast<uint8_t>(limit >> 8U);
    for (unsigned i = 0; i < 8; ++i) {
        ram_.data()[descriptor + 2 + i] = static_cast<uint8_t>(kIdt >> (8U * i));
    }
    write_counter(0);

    cpu_.reset(kCodeBase, kStackTop);
    cpu_.set_gpr(Reg::kRax, descriptor);
    (void)cpu_.run(3); // lidt, sti, nop; queue fires after the next retire

    cpu_.request_interrupt(0x20);
    (void)cpu_.run(20);

    EXPECT_EQ(1U, counter());                                    // handler ran once
    EXPECT_EQ(ExitStatus::Kind::kHalt, cpu_.exit_status().kind); // control came back
    EXPECT_TRUE(cpu_.interrupts_enabled());                      // iretq restored IF from the frame
    EXPECT_EQ(kStackTop, cpu_.gpr(Reg::kRsp));                   // frame balanced
}

TEST_F(InterruptTest, TrapGateKeepsInterruptsEnabled) {
    load({
        0x0F,
        0x01,
        0x18, // lidt [rax]
        0xFB, // sti
        0x90,
    });
    const uint64_t handler_code[] = {0x48, 0xCF}; // iretq only
    for (unsigned i = 0; i < 2; ++i) {
        ram_.data()[kHandler + i] = static_cast<uint8_t>(handler_code[i]);
    }
    install_gate(ram_, 0x21, kHandler, 0x8F); // trap gate (0xF)
    const uint64_t descriptor = kIdt + 0x100;
    ram_.data()[descriptor] = 0x1F;
    ram_.data()[descriptor + 1] = 0x02; // limit 0x21F (covers vector 0x21)
    for (unsigned i = 0; i < 8; ++i) {
        ram_.data()[descriptor + 2 + i] = static_cast<uint8_t>(kIdt >> (8U * i));
    }
    write_counter(0);

    cpu_.reset(kCodeBase, kStackTop);
    cpu_.set_gpr(Reg::kRax, descriptor);
    (void)cpu_.run(5);
    cpu_.request_interrupt(0x21);
    (void)cpu_.run(20);

    EXPECT_EQ(ExitStatus::Kind::kHalt, cpu_.exit_status().kind);
    EXPECT_TRUE(cpu_.interrupts_enabled()); // trap gate never cleared IF
}

TEST_F(InterruptTest, StiShadowDelaysDeliveryPastTheNextInstruction) {
    // SDM STI: after STI opens IF, exactly one more instruction retires
    // before a queued vector is recognized. The handler stamps 9 over the
    // marker at 0x4000; without the shadow it would deliver at the STI
    // retire, run BEFORE the mainline's mov, and the mov's 2 would win.
    load({
        0x0F,
        0x01,
        0x18, // lidt [rax]
        0xFA, // cli
        0xFB, // sti
        0xC7,
        0x04,
        0x25,
        0x00,
        0x40,
        0x00,
        0x00,
        0x02,
        0x00,
        0x00,
        0x00, // mov dword [0x4000], 2
    });
    // handler: mov dword [0x4000], 9; iretq
    const uint8_t handler[] = {
        0xC7, 0x04, 0x25, 0x00, 0x40, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x48, 0xCF,
    };
    for (unsigned i = 0; i < sizeof(handler); ++i) {
        ram_.data()[kHandler + i] = handler[i];
    }
    const uint64_t descriptor = kIdt + 0x100;
    ram_.data()[descriptor] = 0x0F;
    ram_.data()[descriptor + 1] = 0x02; // limit 0x20F (covers vector 0x20)
    for (unsigned i = 0; i < 8; ++i) {
        ram_.data()[descriptor + 2 + i] = static_cast<uint8_t>(kIdt >> (8U * i));
    }
    write_counter(0);

    cpu_.reset(kCodeBase, kStackTop);
    cpu_.set_gpr(Reg::kRax, descriptor);
    (void)cpu_.run(2); // lidt, cli: the queue cannot drain with IF off
    cpu_.request_interrupt(0x20);
    (void)cpu_.run(20); // sti, mov, hlt — delivery must not cut between STI and mov

    EXPECT_EQ(9U, counter()); // handler stamped last, past the shadowed mov
    EXPECT_EQ(ExitStatus::Kind::kHalt, cpu_.exit_status().kind);
}

} // namespace
