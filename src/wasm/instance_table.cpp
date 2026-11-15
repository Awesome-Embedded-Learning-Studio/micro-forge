/**
 * @file instance_table.cpp
 * @brief One Machine per wasm ABI handle; the default instance is lazy.
 * @version 0.1.0
 */

#include "instance_table.hpp"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "guest_ram.hpp"
#include "cinux/cinux_machine.hpp"

// Instance table: one Machine per handle, current() is the one the legacy
// exports act on. Instance 0 stays the lazily created default so the older
// single-machine ABI keeps working unchanged.
namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::vector<std::unique_ptr<micro_forge::Machine>> g_instances;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::size_t g_current = 0;

} // namespace

namespace micro_forge::wasm {

Machine& current() {
    if (g_instances.empty()) {
        g_instances.push_back(std::make_unique<Machine>(kGuestRam, uintptr_t{0}));
    }
    return *g_instances[g_current];
}

uint32_t instance_create(uint32_t ram_size, uint32_t window) {
    const uint64_t ram = ram_size == 0 ? kGuestRam : ram_size;
    auto instance =
        std::make_unique<Machine>(cinux_machine_config(ram, static_cast<uintptr_t>(window)));
    g_instances.push_back(std::move(instance));
    return static_cast<uint32_t>(g_instances.size()); // handle = index + 1
}

void instance_destroy(uint32_t handle) {
    if (handle == 0 || handle > g_instances.size() || handle - 1 == 0) {
        return; // default instance lives for the module
    }
    g_instances[handle - 1].reset();
}

uint32_t instance_use(uint32_t handle) {
    const std::size_t index = handle == 0 ? 0 : handle - 1;
    if (index >= g_instances.size() && index != 0) {
        return 0;
    }
    current(); // ensure the default exists
    if (index >= g_instances.size()) {
        return 0;
    }
    g_current = index;
    return 1;
}

} // namespace micro_forge::wasm

extern "C" {

/// @brief Creates a machine instance over the guest window.
///
/// @param[in] ram_size Usable guest RAM in bytes (0 selects the default).
/// @param[in] window Host window base (0 selects the default window).
/// @return A handle >= 1, or 0 when allocation failed.
uint32_t mf_instance_create(uint32_t ram_size, uint32_t window) {
    return micro_forge::wasm::instance_create(ram_size, window);
}

/// @brief Destroys an instance; the default instance is never destroyed.
/// @param[in] handle Handle from mf_instance_create.
void mf_instance_destroy(uint32_t handle) {
    micro_forge::wasm::instance_destroy(handle);
}

/// @brief Selects the instance the legacy exports act on.
/// @param[in] handle Handle from mf_instance_create (0 = default).
/// @return 1 on success, 0 for an unknown handle.
uint32_t mf_instance_use(uint32_t handle) {
    return micro_forge::wasm::instance_use(handle);
}

} // extern "C"
