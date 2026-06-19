#pragma once

#include <iosfwd>
#include <span>
#include <string_view>

#include "tools/mmio_trace.hpp"

namespace micro_forge::chips::stm32f1 {
class Stm32f103Soc;
}

namespace micro_forge::cli {

// Extra runtime data the caller (CLI) gathered alongside the SoC, folded into
// the snapshot's peripherals / events regions.
struct SnapshotExtras {
    std::span<const tools::MmioAccess> events;
    std::string_view usart_output;
};

// Write a JSON snapshot of the SoC state to `out`.
// Regions: cpu / fault / run / peripherals(usart_output) / events(MMIO ring).
void write_snapshot_json(chips::stm32f1::Stm32f103Soc& soc, std::ostream& out,
                         const SnapshotExtras& extras);

} // namespace micro_forge::cli
