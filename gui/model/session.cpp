// GUI model layer — see session.hpp.
#include "gui/model/session.hpp"

#include "hooks/events.hpp" // hooks::UartByte
#include "tools/memory_dump.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace micro_forge::gui::model {

namespace {

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

bool is_elf(const std::vector<std::uint8_t>& d) {
    return d.size() >= 4 && d[0] == 0x7f && d[1] == 'E' && d[2] == 'L' &&
           d[3] == 'F';
}

} // namespace

Session::Session() = default;

void Session::set_firmware(std::string path) {
    firmware_path_ = std::move(path);
}

std::expected<void, std::string> Session::rebuild() {
    usart_output_.clear();
    auto created = chips::stm32f1::Stm32f103Soc::create();
    if (!created) {
        return std::unexpected(created.error());
    }
    soc_ = std::move(*created);
    soc_->parts().event_bus.uart.connect(
        [this](const hooks::UartByte& e) {
            usart_output_ += static_cast<char>(e.byte);
        });

    if (!firmware_path_.empty()) {
        firmware_data_ = read_file(firmware_path_);
        if (firmware_data_.empty()) {
            return std::unexpected("cannot read: " + firmware_path_);
        }
        auto lr = is_elf(firmware_data_)
                      ? soc_->load_elf(firmware_data_)
                      : soc_->load_bin(0x08000000u, firmware_data_);
        if (!lr) {
            return std::unexpected("load failed: " + lr.error());
        }
    }

    // P2.a WFI fast-forward: env (compat) OR checkbox (fast_forward_). The
    // checkbox state persists across rebuild(); env is a one-shot default.
    if (const char* ff = std::getenv("MICRO_FORGE_FAST_FORWARD")) {
        std::string s(ff);
        if (s == "1" || s == "on" || s == "true") {
            fast_forward_ = true;
        }
    }
    if (fast_forward_) {
        soc_->set_fast_forward_enabled(true);
    }
    if (jit_enabled_) {
        soc_->set_jit_enabled(true);
    }
    return {};
}

void Session::set_fast_forward_enabled(bool on) {
    fast_forward_ = on;
    if (soc_) {
        soc_->set_fast_forward_enabled(on);
    }
}

void Session::set_jit_enabled(bool on) {
    jit_enabled_ = on;
    if (soc_) {
        soc_->set_jit_enabled(on);
    }
}

void Session::run(std::size_t steps) {
    if (soc_) {
        soc_->run(steps);
    }
}

void Session::step() {
    run(1);
}

introspection::IntrospectionSnapshot Session::snapshot() const {
    if (!soc_) {
        return {};
    }
    return introspection::read_introspection(*soc_, usart_output_);
}

void Session::inject_rx(std::uint8_t byte) {
    if (soc_) {
        soc_->parts().usart1.inject_rx(byte);
    }
}

void Session::simulate_gpio_input(char port, std::uint8_t pin, bool high) {
    if (soc_) {
        soc_->parts().gpio(port).simulate_input(pin, high);
    }
}

std::string Session::read_memory(std::uint32_t addr, std::uint32_t len) const {
    if (!soc_) {
        return {};
    }
    std::string out;
    tools::memory_dump(*soc_->machine().bus, addr, len,
        [&out](std::string_view line) {
            out.append(line);
            out.push_back('\n');
        });
    return out;
}

} // namespace micro_forge::gui::model
