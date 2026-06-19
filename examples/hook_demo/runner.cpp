// hook_demo — subscribe to GPIO edge events.
//
// Two ways edges arrive:
//   (1) the real firmware drives them (if it reaches GPIO writes), and
//   (2) a direct set_pin() toggle — proves the hook wiring unconditionally.
// Plus an RCC diagnostic so you can see where the firmware actually got to.
//
//   ./hook_demo <path/to/F103.axf>
#include "chips/stm32f1/stm32f103_soc.hpp"
#include "hooks/ring_sink.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace micro_forge;
using namespace micro_forge::chips::stm32f1;

static std::vector<uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

int main(int argc, char** argv) {
    const char* elf_path = (argc > 1) ? argv[1] : "F103.axf";
    auto data = read_file(elf_path);
    if (data.empty()) {
        std::fprintf(stderr, "cannot read firmware: %s\n", elf_path);
        return 1;
    }

    auto soc = Stm32f103Soc::create();
    if (!soc) {
        std::fprintf(stderr, "SoC create failed: %s\n", soc.error().c_str());
        return 1;
    }

    // Two subscribers on the same signal: a live printer + a non-blocking
    // RingSink (batch collector, drained after the run).
    hooks::RingSink<hooks::GpioEdge> ring(1024);
    auto& gpioa = (*soc)->parts().gpioa;
    gpioa.edge_signal().connect([](const hooks::GpioEdge& e) {
        std::printf("[live ] GPIO%c.%u %s @ cycle %llu\n", e.port, e.pin,
                    e.rising ? "RISE" : "FALL",
                    static_cast<unsigned long long>(e.cycle));
    });
    gpioa.edge_signal().connect(ring.slot());

    auto lr = (*soc)->load_elf(data);
    if (!lr) {
        std::fprintf(stderr, "firmware load failed: %s\n", lr.error().c_str());
        return 1;
    }

    std::printf("--- running firmware (max 2,000,000 steps) ---\n");
    (*soc)->run(2'000'000);

    // Where did the firmware get to? (F103 currently spins in SystemClock_Config
    // waiting on CFGR.SWS — surfaced here so it's visible, not hidden.)
    auto* bus = (*soc)->machine().bus.get();
    if (bus) {
        auto cr = bus->read(0x40021000, Width::Word);
        auto cfgr = bus->read(0x40021004, Width::Word);
        if (cr && cfgr) {
            std::printf("[rcc ] CR=0x%08X CFGR=0x%08X  PLLRDY=%d SWS=%u\n",
                        static_cast<unsigned>(*cr), static_cast<unsigned>(*cfgr),
                        static_cast<int>((*cr >> 25) & 1),
                        static_cast<unsigned>((*cfgr >> 2) & 3));
        }
    }

    // Demonstrate the hook directly — a few PA1 toggles. This fires regardless
    // of how far the firmware got, so the mechanism is always visible.
    std::printf("--- direct PA1 toggle demo ---\n");
    gpioa.set_pin(1, true);
    gpioa.set_pin(1, false);
    gpioa.set_pin(1, true);

    auto buffered = ring.drain();
    std::printf("--- drained %zu buffered edge(s), %zu dropped ---\n",
                buffered.size(), ring.dropped());
    return 0;
}
