#include <gtest/gtest.h>

#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "chips/stm32f1/clock_domains.hpp"
#include "chips/stm32f1/interrupt_config.hpp"
#include "chips/stm32f1/memory_bus.hpp"
#include "chips/stm32f1/stm32f1_afio.hpp"
#include "chips/stm32f1/stm32f1_exti.hpp"
#include "chips/stm32f1/stm32f1_gpio.hpp"
#include "chips/stm32f1/stm32f1_timer.hpp"
#include "chips/stm32f1/stm32f1_usart.hpp"
#include "hooks/events.hpp"
#include "memory/bus.hpp"
#include "memory/flat_memory.hpp"
#include "periph/nvic.hpp"
#include "periph/scb.hpp"
#include "periph/systick.hpp"
#include "sim/coordinator.hpp"
#include "sim/virtual_clock.hpp"

using namespace micro_forge;
using namespace micro_forge::cpu::arm::cortex_m3;
using namespace micro_forge::periph;
using namespace micro_forge::sim;
using namespace micro_forge::chips::stm32f1;

// ── Helpers ──

static void store_word(memory::Bus& bus, addr_t addr, uint32_t val) {
    ASSERT_TRUE(bus.write(addr, val, Width::Word).has_value());
}

static void store_halfwords(memory::FlatMemory& mem, addr_t offset,
                            const std::vector<uint16_t>& insns) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(insns.data());
    ASSERT_TRUE(
        mem.load(offset, {bytes, insns.size() * sizeof(uint16_t)}).has_value());
}

class InterruptTest : public ::testing::Test {
  protected:
    static constexpr addr_t kFlashBase = 0x08000000;
    static constexpr addr_t kSramBase = 0x20000000;
    static constexpr uint32_t kInitSp = 0x20005000;

    // Vector table has 32 entries (128 bytes) for exceptions + IRQ 0-15
    // Code starts after vector table at offset 0x100
    static constexpr addr_t kMainCode = kFlashBase + 0x100;
    static constexpr addr_t kHandlerCode = kFlashBase + 0x110;

    memory::FlatMemory flash_{128 * 1024};
    memory::FlatMemory sram_{20 * 1024};
    memory::Bus bus_;
    NvicPeripheral nvic_;
    ScbPeripheral scb_;
    std::unique_ptr<SysTickPeripheral> systick_;
    chips::stm32f1::Stm32f1Timer tim2_;
    chips::stm32f1::Stm32f1Afio afio_;
    chips::stm32f1::Stm32f1Exti exti_;
    chips::stm32f1::Stm32f1Gpio gpioa_{'A'};
    chips::stm32f1::Stm32f1Usart usart1_;
    std::unique_ptr<CortexM3CPU> cpu_;

    void SetUp() override {
        systick_ = std::make_unique<SysTickPeripheral>();
        ASSERT_TRUE(
            chips::stm32f1::configure_memory(bus_, flash_, sram_).has_value());
        ASSERT_TRUE(chips::stm32f1::configure_interrupt_devices(bus_, nvic_,
                                                                *systick_, scb_)
                        .has_value());

        cpu_ = std::make_unique<CortexM3CPU>(bus_.GetWeak());
        cpu_->set_nvic(nvic_);
        cpu_->set_scb(scb_);
        // TIM2 at 0x40000000; UIF edge → NVIC TIM2 line (IRQ 28).
        ASSERT_TRUE(bus_.map(memory::region(0x40000000, 0x400, tim2_.GetWeak()))
                        .has_value());
        tim2_.set_irq_callback([this]() { (void)cpu_->raise_irq(kTim2Irqn); });
        // EXTI: AFIO (0x40010000) + EXTI (0x40010400). GPIOA driven directly
        // via simulate_input (no bus map needed); its edges feed EXTI.
        ASSERT_TRUE(bus_.map(memory::region(0x40010000, 0x400, afio_.GetWeak()))
                        .has_value());
        ASSERT_TRUE(bus_.map(memory::region(0x40010400, 0x400, exti_.GetWeak()))
                        .has_value());
        exti_.set_afio(afio_);
        exti_.set_irq_callback(
            [this](intr::intr_n_t irq) { (void)cpu_->raise_irq(irq); });
        gpioa_.edge_signal().connect(
            [this](const hooks::GpioEdge& e) { exti_.on_gpio_edge(e); });
        // USART1 at 0x40013800; RXNE (RXNEIE) → NVIC USART1 line (IRQ 37).
        ASSERT_TRUE(
            bus_.map(memory::region(0x40013800, 0x400, usart1_.GetWeak()))
                .has_value());
        usart1_.set_irq_callback(
            [this]() { (void)cpu_->raise_irq(kUsart1Irqn); });
        scb_.set_vtor_callback(
            [this](uint32_t vtor) { cpu_->set_vector_table_base(vtor); });
        scb_.set_prigroup_callback(
            [this](uint8_t group) { cpu_->set_prigroup(group); });

        // Wire SysTick callback → CPU system exception
        systick_->set_irq_callback([this]() { cpu_->sys_tick_irq(); });
        ASSERT_TRUE(cpu_->reset().has_value());
        ASSERT_TRUE(cpu_->set_register_value(13, kInitSp).has_value());
        cpu_->launch();
    }

    void store_vector_table_entry(uint32_t index, uint32_t value) {
        store_word(bus_, kFlashBase + index * 4, value);
    }

    void store_instructions(addr_t addr, const std::vector<uint16_t>& insns) {
        store_halfwords(flash_, addr - kFlashBase, insns);
    }
};

// ── Test 1: BX LR return from interrupt ──

TEST_F(InterruptTest, BxLrReturnFromInterrupt) {
    // Vector table
    store_vector_table_entry(0, kInitSp);            // Initial SP
    store_vector_table_entry(1, kMainCode);          // Reset handler
    store_vector_table_entry(16, kHandlerCode | 1u); // IRQ 0 handler

    // Main code: B . (infinite loop, 0xE7FE)
    store_instructions(kMainCode, {0xE7FE});

    // Handler code: BX LR (0x4770)
    store_instructions(kHandlerCode, {0x4770});

    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());

    uint32_t orig_sp = cpu_->register_value(13).value();

    // Enable and pend IRQ 0
    ASSERT_TRUE(nvic_.write(0x000, (1u << 0), Width::Word).has_value());
    nvic_.set_pending(0);

    // Step: CPU enters interrupt handler and executes first handler instruction
    auto step_res = cpu_->step();
    ASSERT_TRUE(step_res.has_value());

    // Verify handler mode entered
    EXPECT_TRUE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->register_value(13).value(), orig_sp - 32);
    EXPECT_EQ(cpu_->register_value(14).value(), 0xFFFFFFF9u);
    EXPECT_EQ(cpu_->pc().value(), kHandlerCode);

    // Step: BX LR → interrupt return
    step_res = cpu_->step();
    ASSERT_TRUE(step_res.has_value());

    EXPECT_FALSE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->register_value(13).value(), orig_sp);
    EXPECT_EQ(cpu_->pc().value(), kMainCode);
}

// ── Test 2: Stack frame layout verification ──

TEST_F(InterruptTest, StackFrameLayout) {
    store_vector_table_entry(0, kInitSp);
    store_vector_table_entry(1, kMainCode);
    store_vector_table_entry(16, kHandlerCode | 1u);

    store_instructions(kMainCode, {0xE7FE});
    store_instructions(kHandlerCode, {0x4770});

    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());
    ASSERT_TRUE(cpu_->set_register_value(0, 0xAABBCCDDu).has_value());
    ASSERT_TRUE(cpu_->set_register_value(3, 0x11223344u).has_value());
    ASSERT_TRUE(cpu_->set_register_value(12, 0x55667788u).has_value());

    uint32_t orig_sp = cpu_->register_value(13).value();

    ASSERT_TRUE(nvic_.write(0x000, 1u, Width::Word).has_value());
    nvic_.set_pending(0);

    auto step_res = cpu_->step();
    ASSERT_TRUE(step_res.has_value());

    uint32_t new_sp = cpu_->register_value(13).value();
    ASSERT_EQ(new_sp, orig_sp - 32);

    // Stack frame: SP+0=R0, SP+4=R1, SP+8=R2, SP+C=R3, SP+10=R12,
    //              SP+14=LR, SP+18=PC, SP+1C=xPSR
    auto r0 = bus_.read(kSramBase + (new_sp - kSramBase) + 0x00, Width::Word);
    auto r3 = bus_.read(kSramBase + (new_sp - kSramBase) + 0x0C, Width::Word);
    auto r12 = bus_.read(kSramBase + (new_sp - kSramBase) + 0x10, Width::Word);
    auto ret_pc =
        bus_.read(kSramBase + (new_sp - kSramBase) + 0x18, Width::Word);

    ASSERT_TRUE(r0.has_value());
    ASSERT_TRUE(r3.has_value());
    ASSERT_TRUE(r12.has_value());
    ASSERT_TRUE(ret_pc.has_value());

    EXPECT_EQ(*r0, 0xAABBCCDDu);
    EXPECT_EQ(*r3, 0x11223344u);
    EXPECT_EQ(*r12, 0x55667788u);
    EXPECT_EQ(*ret_pc, kMainCode);
}

// ── Test 3: SysTick roundtrip via coordinator ──

TEST_F(InterruptTest, SysTickRoundtrip) {
    store_vector_table_entry(0, kInitSp);
    store_vector_table_entry(1, kMainCode);
    // SysTick is system exception 15 → vector table entry 15
    store_vector_table_entry(15, kHandlerCode | 1u);

    store_instructions(kMainCode, {0xE7FE});
    store_instructions(kHandlerCode, {0x4770});

    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());

    // Configure SysTick: LOAD=10, CTRL=ENABLE+TICKINT (no NVIC needed)
    ASSERT_TRUE(bus_.write(0xE000E014, 10, Width::Word).has_value()); // LOAD
    ASSERT_TRUE(bus_.write(0xE000E010, 3, Width::Word).has_value());  // CTRL

    VirtualClock clk(stm32f103_default_clocks);
    SimulationCoordinator coord(std::move(clk));
    coord.set_cpu(cpu_->GetWeak());
    coord.add_tickable(systick_->GetWeak(), domain_index(ClockDomain::Sysclk));

    bool handler_entered = false;
    bool handler_returned = false;

    for (size_t i = 0; i < 30; ++i) {
        auto r = coord.step();
        ASSERT_TRUE(r.has_value());

        if (cpu_->in_handler_mode() && !handler_entered) {
            handler_entered = true;
        }
        if (handler_entered && !cpu_->in_handler_mode()) {
            handler_returned = true;
            break;
        }
    }

    EXPECT_TRUE(handler_entered);
    EXPECT_TRUE(handler_returned);
    EXPECT_FALSE(cpu_->in_handler_mode());
}

// ── Test 4: HardFault reads vector index 3 (not 19) ──

TEST_F(InterruptTest, HardFaultReadsVectorIndex3) {
    uint32_t handler_addr = kFlashBase + 0x200;
    store_vector_table_entry(3, handler_addr | 1u);
    // Trap value at index 19 — if HardFault erroneously reads this index,
    // it would jump to the trap address instead of the real handler.
    store_vector_table_entry(19, 0xDEAD0001u);

    // 0xDE00 = UDF (undefined instruction)
    store_instructions(kMainCode, {0xDE00});
    store_instructions(handler_addr, {0xE7FE}); // B .

    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());

    auto step_res = cpu_->step();
    ASSERT_TRUE(step_res.has_value());

    EXPECT_TRUE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->pc().value_or(0), handler_addr);

    auto st = cpu_->state();
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(*st, cpu::CPU::State::Running);

    auto& fr = cpu_->last_fault();
    ASSERT_TRUE(fr.has_value());
    EXPECT_EQ(fr->kind, cpu::CPU::CPUError::IllegalInstruction);
}

// ── Test 5: HardFault vector is 0 → Faulted ──

TEST_F(InterruptTest, HardFaultVectorZeroCausesFaulted) {
    store_vector_table_entry(3, 0);
    store_instructions(kMainCode, {0xDE00});

    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());

    auto step_res = cpu_->step();
    EXPECT_FALSE(step_res.has_value());

    auto st = cpu_->state();
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(*st, cpu::CPU::State::Faulted);

    auto& fr = cpu_->last_fault();
    ASSERT_TRUE(fr.has_value());
    EXPECT_EQ(fr->kind, cpu::CPU::CPUError::IllegalInstruction);
}

// ── Test 6: VTOR write updates vector base ──

TEST_F(InterruptTest, VtorWriteUpdatesVectorBase) {
    uint32_t new_vtor = kFlashBase + 0x400;
    uint32_t handler_addr = kFlashBase + 0x500;

    // Write new vector table at secondary location: index 3 = HardFault handler
    store_word(bus_, new_vtor + 3 * 4, handler_addr | 1u);

    // Write SCB VTOR (SCB base = 0xE000ED00, VTOR offset = 0x08)
    ASSERT_TRUE(bus_.write(0xE000ED08, new_vtor, Width::Word).has_value());

    // Trigger a fault — should read handler from new VTOR location
    store_instructions(kMainCode, {0xDE00});
    store_instructions(handler_addr, {0xE7FE});
    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());

    auto step_res = cpu_->step();
    ASSERT_TRUE(step_res.has_value());
    EXPECT_TRUE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->pc().value_or(0), handler_addr);
}

TEST_F(InterruptTest, AircrWrongKeyIsCompatibilityNoOp) {
    auto before = scb_.read(0x0C, Width::Word);
    ASSERT_TRUE(before.has_value());

    ASSERT_TRUE(scb_.write(0x0C, 0x12340004, Width::Word).has_value());

    auto after = scb_.read(0x0C, Width::Word);
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(*after, *before);
}

// ── Preemption & nesting ──

// IRQ0 (low, 0xE0) is running when IRQ1 (high, 0x00) pends: IRQ1 must
// pre-empt, run, and return to the middle of IRQ0's handler (nested return).
TEST_F(InterruptTest, HighPriorityPreemptsLow) {
    constexpr addr_t kLowHandler = kFlashBase + 0x110;
    constexpr addr_t kHighHandler = kFlashBase + 0x130;

    store_vector_table_entry(0, kInitSp);
    store_vector_table_entry(1, kMainCode);
    store_vector_table_entry(16, kLowHandler | 1u);  // IRQ 0
    store_vector_table_entry(17, kHighHandler | 1u); // IRQ 1

    // IRQ0 = 0xE0 (low), IRQ1 = 0x00 (high). Both are < 0xF0 so each can enter
    // from thread mode (whose active preemption level is 0xF).
    ASSERT_TRUE(
        bus_.write(0xE000E400, 0xE0u | (0x00u << 8), Width::Word).has_value());
    ASSERT_TRUE(
        bus_.write(0xE000E100, (1u << 0) | (1u << 1), Width::Word).has_value());

    store_instructions(kMainCode, {0xE7FE}); // B .
    store_instructions(kLowHandler,
                       {0xBF00, 0xBF00, 0x4770});       // NOP; NOP; BX LR
    store_instructions(kHighHandler, {0xBF00, 0x4770}); // NOP; BX LR

    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());

    nvic_.set_pending(0);
    ASSERT_TRUE(cpu_->step().has_value()); // enter low handler
    EXPECT_TRUE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->pc().value(), kLowHandler);

    ASSERT_TRUE(cpu_->step().has_value()); // 1st NOP → PC = kLowHandler + 2

    // Pend the high-priority IRQ → must pre-empt
    nvic_.set_pending(1);
    ASSERT_TRUE(cpu_->step().has_value()); // pre-empt → enter high handler
    EXPECT_TRUE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->pc().value(), kHighHandler);

    ASSERT_TRUE(cpu_->step().has_value()); // high: NOP
    ASSERT_TRUE(cpu_->step().has_value()); // high: BX LR → nested return to low
    EXPECT_TRUE(cpu_->in_handler_mode());  // still in handler (low resumed)
    EXPECT_EQ(cpu_->pc().value(), kLowHandler + 2);

    ASSERT_TRUE(cpu_->step().has_value()); // 2nd NOP
    ASSERT_TRUE(cpu_->step().has_value()); // BX LR → return to thread
    EXPECT_FALSE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->pc().value(), kMainCode);
}

// Two IRQs at equal priority: the second must NOT pre-empt the first.
TEST_F(InterruptTest, SamePriorityDoesNotPreempt) {
    constexpr addr_t kHandlerA = kFlashBase + 0x110;
    constexpr addr_t kHandlerB = kFlashBase + 0x130;

    store_vector_table_entry(0, kInitSp);
    store_vector_table_entry(1, kMainCode);
    store_vector_table_entry(16, kHandlerA | 1u); // IRQ 0
    store_vector_table_entry(17, kHandlerB | 1u); // IRQ 1

    ASSERT_TRUE(
        bus_.write(0xE000E400, 0xE0u | (0xE0u << 8), Width::Word).has_value());
    ASSERT_TRUE(
        bus_.write(0xE000E100, (1u << 0) | (1u << 1), Width::Word).has_value());

    store_instructions(kMainCode, {0xE7FE});
    store_instructions(kHandlerA, {0xBF00, 0xBF00, 0x4770});
    store_instructions(kHandlerB, {0xBF00, 0x4770});

    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());

    nvic_.set_pending(0);
    ASSERT_TRUE(cpu_->step().has_value()); // enter handler A
    EXPECT_EQ(cpu_->pc().value(), kHandlerA);

    ASSERT_TRUE(cpu_->step().has_value()); // 1st NOP

    // Pend IRQ1 at equal priority → must NOT pre-empt; A keeps running
    nvic_.set_pending(1);
    ASSERT_TRUE(cpu_->step().has_value()); // executes 2nd NOP, stays in A
    EXPECT_TRUE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->pc().value(), kHandlerA + 4); // advanced to BX LR slot
}

// BASEPRI masks IRQs whose group priority >= BASEPRI. With BASEPRI=0x80, the
// low IRQ (0xE0) is masked while the high IRQ (0x00) still enters.
TEST_F(InterruptTest, BasepriMasksLowerPriority) {
    constexpr addr_t kLowHandler = kFlashBase + 0x110;
    constexpr addr_t kHighHandler = kFlashBase + 0x130;

    store_vector_table_entry(0, kInitSp);
    store_vector_table_entry(1, kMainCode);
    store_vector_table_entry(16, kLowHandler | 1u);
    store_vector_table_entry(17, kHighHandler | 1u);

    // IRQ0 = 0xE0 (low), IRQ1 = 0x00 (high)
    ASSERT_TRUE(
        bus_.write(0xE000E400, 0xE0u | (0x00u << 8), Width::Word).has_value());
    ASSERT_TRUE(
        bus_.write(0xE000E100, (1u << 0) | (1u << 1), Width::Word).has_value());

    // Main: MSR BASEPRI,R3 ; B .   (MSR BASEPRI = 0xF383 0x8811)
    store_instructions(kMainCode, {0xF383, 0x8811, 0xE7FE});
    store_instructions(kLowHandler, {0xBF00, 0x4770});
    store_instructions(kHighHandler, {0xBF00, 0x4770});

    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());
    ASSERT_TRUE(
        cpu_->set_register_value(3, 0x80u).has_value()); // R3 = BASEPRI value

    ASSERT_TRUE(cpu_->step().has_value()); // MSR BASEPRI,R3 → basepri_ = 0x80
    ASSERT_TRUE(cpu_->step().has_value()); // B .

    // IRQ0 (0xE0, group 0xE) is masked by BASEPRI 0x80 (group 8) → not entered
    nvic_.set_pending(0);
    ASSERT_TRUE(cpu_->step().has_value()); // stays in thread
    EXPECT_FALSE(cpu_->in_handler_mode());

    // IRQ1 (0x00, group 0) is above BASEPRI → enters
    nvic_.set_pending(1);
    ASSERT_TRUE(cpu_->step().has_value());
    EXPECT_TRUE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->pc().value(), kHighHandler);
}

// Thread mode on PSP: exception entry must stack on MSP, set EXC_RETURN=FD,
// and on return switch back to PSP (verifies the MSP/PSP shadow invariant).
TEST_F(InterruptTest, ThreadUsesPspOnException) {
    constexpr addr_t kHandler = kFlashBase + 0x110;
    constexpr uint32_t kPsp = 0x20004000u;

    store_vector_table_entry(0, kInitSp); // initial MSP
    store_vector_table_entry(1, kMainCode);
    store_vector_table_entry(16, kHandler | 1u); // IRQ 0

    ASSERT_TRUE(bus_.write(0xE000E100, (1u << 0), Width::Word).has_value());

    // Main: MSR PSP,R1 ; MSR CONTROL,R2 ; B .
    store_instructions(kMainCode, {0xF381, 0x8809, 0xF382, 0x8814, 0xE7FE});
    store_instructions(kHandler, {0x4770}); // BX LR

    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());
    ASSERT_TRUE(cpu_->set_register_value(1, kPsp).has_value()); // R1 = PSP
    ASSERT_TRUE(
        cpu_->set_register_value(2, 0x2u).has_value()); // R2 = CONTROL(SPSEL=1)

    ASSERT_TRUE(cpu_->step().has_value()); // MSR PSP,R1
    ASSERT_TRUE(cpu_->step().has_value()); // MSR CONTROL,R2 → active SP = PSP
    EXPECT_EQ(cpu_->register_value(13).value(), kPsp);
    ASSERT_TRUE(cpu_->step().has_value()); // B .

    // Pend IRQ0 → entry stacks on MSP and returns via EXC_RETURN=FD
    nvic_.set_pending(0);
    ASSERT_TRUE(cpu_->step().has_value()); // enter handler
    EXPECT_TRUE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->register_value(14).value(), 0xFFFFFFFDu);
    EXPECT_EQ(cpu_->register_value(13).value(),
              kInitSp - 32u); // stacked on MSP

    ASSERT_TRUE(cpu_->step().has_value()); // BX LR → return to thread/PSP
    EXPECT_FALSE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->register_value(13).value(), kPsp); // PSP restored
}

// AIRCR.PRIGROUP changes the preemption grouping. Under PRIGROUP=4 (3 preempt
// bits), IRQ0=0x10 and IRQ1=0x01 share preemption group 0, so IRQ1 cannot
// pre-empt IRQ0 (unlike the default 4-preempt-bit grouping, where it would).
TEST_F(InterruptTest, PriorityGroupingAffectsPreemption) {
    constexpr addr_t kHandlerA = kFlashBase + 0x110;
    constexpr addr_t kHandlerB = kFlashBase + 0x130;

    store_vector_table_entry(0, kInitSp);
    store_vector_table_entry(1, kMainCode);
    store_vector_table_entry(16, kHandlerA | 1u); // IRQ 0
    store_vector_table_entry(17, kHandlerB | 1u); // IRQ 1

    // IRQ0 = 0x10, IRQ1 = 0x01
    ASSERT_TRUE(
        bus_.write(0xE000E400, 0x10u | (0x01u << 8), Width::Word).has_value());
    ASSERT_TRUE(
        bus_.write(0xE000E100, (1u << 0) | (1u << 1), Width::Word).has_value());

    store_instructions(kMainCode, {0xE7FE});
    store_instructions(kHandlerA, {0xBF00, 0xBF00, 0x4770});
    store_instructions(kHandlerB, {0xBF00, 0x4770});

    // AIRCR: VECTKEY=0x05FA, PRIGROUP=4 (bits[10:8]) → 0x05FA0400.
    ASSERT_TRUE(bus_.write(0xE000ED0C, 0x05FA0400u, Width::Word).has_value());

    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());

    nvic_.set_pending(0);
    ASSERT_TRUE(cpu_->step().has_value()); // enter handler A
    EXPECT_EQ(cpu_->pc().value(), kHandlerA);
    ASSERT_TRUE(cpu_->step().has_value()); // 1st NOP

    // IRQ1 (0x01) shares IRQ0's (0x10) preemption group under PRIGROUP=4 →
    // must NOT pre-empt.
    nvic_.set_pending(1);
    ASSERT_TRUE(cpu_->step().has_value()); // stays in handler A
    EXPECT_TRUE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->pc().value(), kHandlerA + 4);
}

// ── Timer UIF → NVIC → handler roundtrip (C2) ──
// Coordinator drives TIM2 tick → UIF edge → raise_irq(28) → NVIC pending →
// handler entry → BX LR return. First end-to-end use of the raise_irq channel.
TEST_F(InterruptTest, TimerUifRoundtrip) {
    constexpr addr_t kHandler = kFlashBase + 0x110;
    store_vector_table_entry(0, kInitSp);
    store_vector_table_entry(1, kMainCode);
    store_vector_table_entry(16 + kTim2Irqn, kHandler | 1u); // TIM2 → vector 44
    store_instructions(kMainCode, {0xE7FE});                 // B .
    store_instructions(kHandler, {0x4770});                  // BX LR
    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());

    // NVIC: enable TIM2 (bit 28), priority 0xE0 (below thread 0xF0).
    ASSERT_TRUE(
        bus_.write(0xE000E100, 1u << kTim2Irqn, Width::Word).has_value());
    ASSERT_TRUE(
        bus_.write(0xE000E400 + kTim2Irqn, 0xE0u, Width::Word).has_value());

    // TIM2: PSC=0, ARR=5, DIER.UIE, CR1.CEN
    ASSERT_TRUE(bus_.write(0x40000028, 0u, Width::Word).has_value()); // PSC
    ASSERT_TRUE(bus_.write(0x4000002C, 5u, Width::Word).has_value()); // ARR
    ASSERT_TRUE(
        bus_.write(0x4000000C, 1u, Width::Word).has_value()); // DIER.UIE
    ASSERT_TRUE(bus_.write(0x40000000, 1u, Width::Word).has_value()); // CR1.CEN

    VirtualClock clk(stm32f103_default_clocks);
    SimulationCoordinator coord(std::move(clk));
    coord.set_cpu(cpu_->GetWeak());
    coord.add_tickable(tim2_.GetWeak(), domain_index(ClockDomain::Apb1));

    bool entered = false, returned = false;
    for (size_t i = 0; i < 80; ++i) {
        ASSERT_TRUE(coord.step().has_value());
        if (cpu_->in_handler_mode() && !entered) {
            entered = true;
        }
        if (entered && !cpu_->in_handler_mode()) {
            returned = true;
            break;
        }
    }
    EXPECT_TRUE(entered)
        << "TIM2 UIF did not raise an IRQ that entered the handler";
    EXPECT_TRUE(returned) << "TIM2 handler did not return";
}

// ── EXTI GPIO edge → NVIC → handler roundtrip (C1) ──
// simulate_input rising edge on PA2 → GPIO edge_signal → EXTI (EXTICR routes
// line 2 to PA, IMR+RTSR match) → raise EXTI2 IRQ (8) → handler entry/return.
TEST_F(InterruptTest, ExtiGpioEdgeRoundtrip) {
    constexpr addr_t kHandler = kFlashBase + 0x110;
    store_vector_table_entry(0, kInitSp);
    store_vector_table_entry(1, kMainCode);
    store_vector_table_entry(16 + 8, kHandler | 1u); // EXTI2 → IRQ8 → vector 24
    store_instructions(kMainCode, {0xE7FE});         // B .
    store_instructions(kHandler, {0x4770});          // BX LR
    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());

    // NVIC: enable EXTI2 (IRQ8), priority 0xE0 (IPR2 byte0, word-aligned).
    ASSERT_TRUE(bus_.write(0xE000E100, 1u << 8, Width::Word).has_value());
    ASSERT_TRUE(bus_.write(0xE000E408, 0xE0u, Width::Word).has_value());

    // AFIO EXTICR1: line 2 → port A (0). EXTI: IMR line2 + RTSR line2.
    ASSERT_TRUE(bus_.write(0x40010008, 0u, Width::Word).has_value()); // EXTICR1
    ASSERT_TRUE(
        bus_.write(0x40010400, 1u << 2, Width::Word).has_value()); // IMR
    ASSERT_TRUE(
        bus_.write(0x40010408, 1u << 2, Width::Word).has_value()); // RTSR

    gpioa_.simulate_input(2, true); // rising edge PA2 → EXTI → raise IRQ8

    ASSERT_TRUE(cpu_->step().has_value()); // pending → enter handler
    EXPECT_TRUE(cpu_->in_handler_mode());
    EXPECT_EQ(cpu_->pc().value(), kHandler);
    ASSERT_TRUE(cpu_->step().has_value()); // BX LR → return
    EXPECT_FALSE(cpu_->in_handler_mode());
}

// ── USART RXNE → NVIC → handler roundtrip (C3) ──
// inject_rx → RXNE + RXNEIE → raise USART1 IRQ (37) → handler reads DR.
TEST_F(InterruptTest, UsartRxRoundtrip) {
    constexpr addr_t kHandler = kFlashBase + 0x110;
    store_vector_table_entry(0, kInitSp);
    store_vector_table_entry(1, kMainCode);
    store_vector_table_entry(16 + 37,
                             kHandler | 1u); // USART1 → IRQ37 → vector 53
    store_instructions(kMainCode, {0xE7FE}); // B .
    // handler: ldr r4, [r1] (r1=USART1 DR) ; bx lr. r4 is not auto-stacked, so
    // it survives exception return (r0-r3 are restored by the POP).
    store_instructions(kHandler, {0x680C, 0x4770}); // ldr r4,[r1,#0] ; bx lr
    ASSERT_TRUE(cpu_->set_pc(kMainCode).has_value());
    ASSERT_TRUE(
        cpu_->set_register_value(1, 0x40013804u).has_value()); // r1 = DR

    // NVIC: enable USART1 (IRQ37 → ISER1 bit5), priority 0xE0 (IPR9 byte1).
    ASSERT_TRUE(bus_.write(0xE000E104, 1u << 5, Width::Word).has_value());
    ASSERT_TRUE(bus_.write(0xE000E424, 0xE0u << 8, Width::Word).has_value());

    // USART1 CR1: UE(13) + RXNEIE(5) + RE(2).
    ASSERT_TRUE(
        bus_.write(0x4001380C, (1u << 13) | (1u << 5) | (1u << 2), Width::Word)
            .has_value());

    usart1_.inject_rx('A'); // → RXNE + raise IRQ37

    ASSERT_TRUE(cpu_->step().has_value()); // pending → enter handler
    EXPECT_TRUE(cpu_->in_handler_mode());
    ASSERT_TRUE(cpu_->step().has_value()); // ldr r0,[r1] → r0='A', RXNE cleared
    ASSERT_TRUE(cpu_->step().has_value()); // bx lr → return
    EXPECT_FALSE(cpu_->in_handler_mode());
    auto r4 = cpu_->register_value(4);
    ASSERT_TRUE(r4.has_value());
    EXPECT_EQ(*r4, static_cast<uint32_t>('A'));
}
