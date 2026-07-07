// Structured introspection — the single source of truth for observable state.
// Pure read: pulls CPU registers, fault record, run cycles and peripheral
// state into plain structs that both the CLI JSON serializer and the GUI
// dashboard consume. Owns no serialization format of its own.
#include "introspection/introspection.hpp"

#include "arch/arm/cortex_m3/cortex_m3.hpp"
#include "chips/stm32f1/soc/stm32f103_soc.hpp"
#include "cpu/cpu.hpp"

#include <cstddef>
#include <cstdint>

using namespace micro_forge;
using micro_forge::cpu::CPU;
using micro_forge::chips::stm32f1::Stm32f103Soc;

namespace micro_forge::introspection {

IntrospectionSnapshot read_introspection(Stm32f103Soc& soc,
                                         std::string_view usart_output) noexcept {
    IntrospectionSnapshot snap{};

    // Peripherals are readable whether or not the CPU is wired (parts() always
    // exists), so fill them first.
    auto& p = snap.peripherals;
    p.usart_output = usart_output;
    auto& parts = soc.parts();
    p.gpio[0] = {'A', static_cast<uint16_t>(parts.gpioa.odr() & 0xFFFFu)};
    p.gpio[1] = {'B', static_cast<uint16_t>(parts.gpiob.odr() & 0xFFFFu)};
    p.gpio[2] = {'C', static_cast<uint16_t>(parts.gpioc.odr() & 0xFFFFu)};
    p.systick = {parts.systick.ctrl(), parts.systick.load(),
                 parts.systick.val()};
    p.nvic.has_pending = parts.nvic.has_pending_irq();
    p.nvic.highest_pending_irq = parts.nvic.highest_pending_irq();
    p.nvic.enabled_count = parts.nvic.enabled_count();
    p.scb = {parts.scb.icsr(), parts.scb.vtor(), parts.scb.aircr(),
             parts.scb.prigroup()};

    auto cm3 = soc.cortex_m3_cpu();
    if (!cm3.IsValid()) {
        return snap;
    }

    auto& cpu = snap.cpu;
    cpu.state = cm3->state().value_or(CPU::State::Halted);
    cpu.handler_mode = cm3->in_handler_mode();
    cpu.pc = static_cast<uint32_t>(cm3->pc().value_or(0));
    cpu.lr = static_cast<uint32_t>(cm3->register_value(14).value_or(0));
    cpu.sp = static_cast<uint32_t>(cm3->register_value(13).value_or(0));
    for (std::size_t r = 0; r < cpu.regs.size(); ++r) {
        cpu.regs[r] = static_cast<uint32_t>(cm3->register_value(r).value_or(0));
    }
    cpu.xpsr = static_cast<uint32_t>(cm3->xpsr());
    cpu.primask = static_cast<uint32_t>(cm3->primask());
    cpu.basepri = static_cast<uint32_t>(cm3->basepri());
    cpu.faultmask = static_cast<uint32_t>(cm3->faultmask());
    cpu.control = static_cast<uint32_t>(cm3->control());
    cpu.msp = static_cast<uint32_t>(cm3->msp());
    cpu.psp = static_cast<uint32_t>(cm3->psp());

    const auto& fr = cm3->last_fault();
    if (fr.has_value()) {
        auto& f = snap.fault;
        f.present = true;
        f.kind = fr->kind;
        f.pc = static_cast<uint32_t>(fr->pc);
        f.lr = static_cast<uint32_t>(fr->lr);
        f.sp = static_cast<uint32_t>(fr->sp);
        f.xpsr = static_cast<uint32_t>(fr->xpsr);
        f.is_32bit = fr->is_32bit;
        f.opcode16 = fr->opcode16;
        f.opcode16_2 = fr->opcode16_2;
        if (fr->bus_error) {
            f.has_bus_error = true;
            f.bus_error_raw = static_cast<uint32_t>(*fr->bus_error);
        }
        if (fr->access_addr) {
            f.has_access_addr = true;
            f.access_addr = static_cast<uint32_t>(*fr->access_addr);
        }
    }

    snap.cycles = soc.machine().cpu->cycles().value_or(0);
    return snap;
}

} // namespace micro_forge::introspection
