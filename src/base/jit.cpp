/**
 * @file jit.cpp
 * @brief Copy-and-patch JIT: page management and template emission.
 * @version 0.1.0
 */

#include "micro_forge/base/jit.hpp"

#include <array>
#include <cstring>

#include <sys/mman.h>
#include <unistd.h>

namespace micro_forge::base {

namespace {

// mov byte ptr [rdi], imm8 ; ret  (hole = the imm8 at offset 2)
constexpr std::array<std::uint8_t, 4> kMovAlImm8 = {0xC6, 0x07, 0x00, 0xC3};
// add qword ptr [rdi], imm32 ; ret  (hole = the imm32 at offset 3)
constexpr std::array<std::uint8_t, 8> kAddRaxImm32 = {
    0x48, 0x81, 0x07, 0x00, 0x00, 0x00, 0x00, 0xC3,
};

} // namespace

TemplateSpec template_spec(JitTemplate id) {
    switch (id) {
    case JitTemplate::kMovAlImm8:
        return TemplateSpec{.bytes = kMovAlImm8, .hole_offset = 2, .hole_size = 1};
    case JitTemplate::kAddRaxImm32:
        return TemplateSpec{.bytes = kAddRaxImm32, .hole_offset = 3, .hole_size = 4};
    }
    return TemplateSpec{};
}

JitPage::JitPage() = default;

JitPage::~JitPage() {
    if (data_ != nullptr) {
        (void)munmap(data_, kSize);
    }
}

JitPage::JitPage(JitPage&& other) noexcept
    : data_(other.data_), used_(other.used_), executable_(other.executable_) {
    other.data_ = nullptr;
    other.used_ = 0;
    other.executable_ = false;
}

JitPage& JitPage::operator=(JitPage&& other) noexcept {
    if (this != &other) {
        if (data_ != nullptr) {
            (void)munmap(data_, kSize);
        }
        data_ = other.data_;
        used_ = other.used_;
        executable_ = other.executable_;
        other.data_ = nullptr;
        other.used_ = 0;
        other.executable_ = false;
    }
    return *this;
}

std::optional<JitPage> JitPage::create() {
    // Keep the existing low-address page allocation policy. Helper calls
    // carry full 64-bit addresses and do not depend on this placement.
    void* page = mmap(nullptr, kSize, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (page == MAP_FAILED) {
        return std::nullopt;
    }
    JitPage result;
    result.data_ = static_cast<std::uint8_t*>(page);
    return result;
}

std::optional<std::size_t> JitPage::emit(std::span<const std::uint8_t> code,
                                         std::size_t hole_offset, std::size_t hole_size,
                                         std::uint64_t value) {
    if (data_ == nullptr || executable_ || code.empty() || hole_size == 0 || hole_size > 8 ||
        hole_offset + hole_size > code.size() || used_ + code.size() > kSize) {
        return std::nullopt;
    }
    const std::size_t offset = used_;
    std::memcpy(data_ + offset, code.data(), code.size());
    std::memcpy(data_ + offset + hole_offset, &value, hole_size);
    used_ += code.size();
    return offset;
}

std::optional<std::size_t> JitPage::emit_multi(std::span<const std::uint8_t> code,
                                               std::span<const JitPage::Patch> patches) {
    if (data_ == nullptr || executable_ || code.empty() || used_ + code.size() > kSize) {
        return std::nullopt;
    }
    for (const Patch& patch : patches) {
        if (patch.size == 0 || patch.size > 8 || patch.offset + patch.size > code.size()) {
            return std::nullopt;
        }
    }
    const std::size_t offset = used_;
    std::memcpy(data_ + offset, code.data(), code.size());
    for (const Patch& patch : patches) {
        std::memcpy(data_ + offset + patch.offset, &patch.value, patch.size);
    }
    used_ += code.size();
    return offset;
}

bool JitPage::patch(std::size_t offset, std::size_t size, std::uint64_t value) {
    if (data_ == nullptr || executable_ || size == 0 || size > 8 || offset + size > used_) {
        return false;
    }
    std::memcpy(data_ + offset, &value, size);
    return true;
}

bool JitPage::unseal() {
    if (data_ == nullptr || !executable_) {
        return false;
    }
    if (mprotect(data_, kSize, PROT_READ | PROT_WRITE) != 0) {
        return false;
    }
    executable_ = false;
    return true;
}

bool JitPage::seal() {
    if (data_ == nullptr || executable_) {
        return false;
    }
    if (mprotect(data_, kSize, PROT_READ | PROT_EXEC) != 0) {
        return false;
    }
    executable_ = true;
    return true;
}

void JitPage::invoke(std::size_t offset, std::uint64_t* regs) const {
    const auto entry = reinterpret_cast<void (*)(std::uint64_t*)>(data_ + offset);
    entry(regs);
}

} // namespace micro_forge::base
