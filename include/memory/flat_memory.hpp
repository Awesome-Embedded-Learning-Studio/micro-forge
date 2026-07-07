#pragma once

#include "periph/device.hpp"
#include "util/weak_ptr/weak_ptr_factory.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace micro_forge::memory {

class FlatMemory : public periph::Device {
  public:
    explicit FlatMemory(addr_t size);
    ~FlatMemory() override = default;

    Expected<data_t> read(addr_t offset, Width w) override;
    Expected<void> write(addr_t offset, data_t data, Width w) override;
    std::string_view name() const noexcept override { return "FlatMemory"; }

    Expected<void> load(addr_t offset, std::span<const uint8_t> data);

    WeakPtr<FlatMemory> GetWeak() { return weak_factory_.GetWeakPtr(); }
    addr_t size() const { return size_; }

  private:
    std::vector<uint8_t> data_;
    addr_t size_;
    WeakPtrFactory<FlatMemory> weak_factory_{this};
};

} // namespace micro_forge::memory
