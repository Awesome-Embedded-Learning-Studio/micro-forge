#include "arch/toy/isa.hpp"
#include "autogen/arch_details.hpp"
#include "core/types.hpp"
#include "cpu/cpu.hpp"
#include "cpu/intr.hpp"
#include "cpu/regfile.hpp"
#include "memory/bus.hpp"
#include "util/weak_ptr/weak_ptr.hpp"
#include <array>
#include <cstdint>

namespace micro_forge::cpu {

struct ExecInfo {
    bool branched = false;
};

class ToyCPU : public CPU {
  public:
    /**
     * @brief Tell me memory bus, cpu needs to communicate this
     *
     * @param memory_bus
     */
    ToyCPU(WeakPtr<memory::Bus> memory_bus);
    /**
     * @brief We can set the intr handler, by specifing the address of handler
     *
     * @param irq_num
     * @param handler_addr
     */
    void set_interrupt_vector(intr::intr_n_t irq_num, addr_t handler_addr);

    CPUExpected<void> reset() override;
    CPUExpected<void> step() override;
    CPUExpected<State> state() const noexcept override {
        return current_status_;
    }
    CPUExpected<data_t> register_value(const std::size_t index) const override;
    CPUExpected<void> set_register_value(const std::size_t index,
                                         data_t value) override;
    CPUExpected<std::string_view> register_name(std::size_t idx) const override;
    CPUExpected<std::size_t> register_count() const override {
        return toy::RegCount;
    }

    CPUExpected<addr_t> pc() const override { return pc_; }
    CPUExpected<addr_t> set_pc(addr_t new_pc) override {
        pc_ = new_pc;
        return pc_;
    }
    CPUExpected<void> raise_irq(intr::intr_n_t irq_index) override;
    CPUExpected<ticks_t> cycles() const override { return cycles_; }

    void start() { current_status_ = State::Running; }

  private:
    /**
     * @brief Let CPU fetch the next instructions
     *
     * @return Expected<data_t>
     */
    Expected<data_t> fetch_instructions();
    CPUExpected<ExecInfo> execute_target_instructions(const data_t instrcutons);
    void update_flags(data_t result);

    /* Sync Check the Intr, poll it if inst chip has */
    CPUExpected<void> poll_intr();
    Expected<void> push_stack(data_t val);
    Expected<data_t> pop_stack();

  private:
    WeakPtr<memory::Bus> memory_bus_;
    reg::Registers<toy::RegCount> registers_;
    addr_t pc_{0};
    bool zf_ = false;
    bool nf_ = false;
    State current_status_ = State::Halted;
    uint64_t cycles_ = 0;

    std::array<addr_t, toy::INTR_CNT> intr_vec_;
    uint32_t pending_irq_mask_ = 0;
};
} // namespace micro_forge::cpu