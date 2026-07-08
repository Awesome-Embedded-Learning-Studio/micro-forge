// GUI model layer — Qt-free façade over the simulated STM32F103 SoC.
//
// Owns the SoC, advances steps, accumulates USART output, and produces the
// structured IntrospectionSnapshot the view renders. Kept Qt-free so it can
// be unit-tested without a QApplication. Per DIRECTIVES §E the sim stays
// single-threaded; the view's QTimer drives run()/step() from the Qt main
// thread.
#pragma once

#include "chips/stm32f1/soc/stm32f103_soc.hpp"
#include "introspection/introspection.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace micro_forge::gui::model {

class Session {
  public:
    Session();

    // Firmware path to load on the next rebuild(); empty = no firmware.
    void set_firmware(std::string path);

    // Create the SoC, subscribe its UART event source, and (re)load firmware.
    // Returns an error string on create / read / load failure; the session
    // stays invalid (snapshot() yields a default-constructed snapshot).
    std::expected<void, std::string> rebuild();

    // Advance the simulator. No-op when invalid.
    void run(std::size_t steps);
    void step(); // = run(1)

    // True when a valid SoC is loaded.
    bool valid() const noexcept { return soc_ != nullptr; }

    // Structured snapshot of CPU + fault + peripherals (default-constructed,
    // i.e. state=Halted / all-zero, when invalid).
    introspection::IntrospectionSnapshot snapshot() const;

    // USART bytes accumulated since rebuild(); the view drains this per tick.
    std::string_view usart_output() const noexcept { return usart_output_; }

    // Inject external input into the simulated peripherals (A2). Forwarded to
    // the SoC; no-op when invalid (no SoC loaded).
    void inject_rx(std::uint8_t byte);
    void simulate_gpio_input(char port, std::uint8_t pin, bool high);

    // Hex dump of a memory region (C4-mem). Returns formatted lines joined by
    // newlines; empty when invalid. Read-only — safe from the GUI tick thread.
    std::string read_memory(std::uint32_t addr, std::uint32_t len) const;

  private:
    std::unique_ptr<chips::stm32f1::Stm32f103Soc> soc_;
    std::string usart_output_;
    std::string firmware_path_;
    std::vector<std::uint8_t> firmware_data_;
};

} // namespace micro_forge::gui::model
