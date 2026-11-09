/**
 * @file jit.hpp
 * @brief Copy-and-patch JIT: executable pages plus patched templates.
 * @version 0.1.0
 *
 * A template is a fixed byte sequence with one 8-byte hole; the JIT
 * copies the template into an executable page and writes the hole value
 * (an immediate, a displacement, a register slot offset). Control then
 * enters the page like any function pointer. This is the native shape of
 * copy-and-patch; it needs an executable mapping, which WebAssembly does
 * not offer (the wasm engine keeps the interpreter).
 *
 * Scope today: the page manager, template emission, and a tiny template
 * set used by the tests. The engine-side lowering (guest instruction to
 * template) lands incrementally.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace micro_forge::base {

/**
 * @brief One executable page of JIT code.
 *
 * Pages are allocated RW, filled, then flipped to RX with mprotect; the
 * W^X flip means code cannot be written while it can execute.
 */
class JitPage {
public:
    /// Typical page size on x86-64 Linux; the kernel may report 4 KiB.
    static constexpr std::size_t kSize = 4096;

    JitPage();
    ~JitPage();
    JitPage(const JitPage&) = delete;
    JitPage& operator=(const JitPage&) = delete;
    JitPage(JitPage&& other) noexcept;
    JitPage& operator=(JitPage&& other) noexcept;

    /// @brief Allocates one page (RW). Empty on failure.
    [[nodiscard]] static std::optional<JitPage> create();

    /// @brief Bytes still free in the page.
    [[nodiscard]] std::size_t available() const { return kSize - used_; }
    /// @brief Bytes already emitted.
    [[nodiscard]] std::size_t used() const { return used_; }
    /// @brief Base address of the page (valid while mapped).
    [[nodiscard]] std::uint8_t* base() const { return data_; }

    /**
     * @brief Appends a template, patching its single 8-byte hole.
     *
     * @param[in] code Template bytes (with one hole placeholder).
     * @param[in] hole_offset Offset of the hole inside @p code.
     * @param[in] hole_size Hole width in bytes (1, 4, or 8).
     * @param[in] value Value written into the hole (little endian).
     * @return Offset of the emitted block inside the page, or nullopt
     *         when the page lacks room.
     */
    [[nodiscard]] std::optional<std::size_t> emit(std::span<const std::uint8_t> code,
                                                  std::size_t hole_offset, std::size_t hole_size,
                                                  std::uint64_t value);

    /// One hole patch: where, how wide, what value.
    struct Patch {
        std::size_t offset = 0;  ///< Byte offset inside the template.
        std::size_t size = 0;    ///< 1, 4, or 8 bytes.
        std::uint64_t value = 0; ///< Little-endian value written there.
    };

    /**
     * @brief Appends a template patching every hole in @p patches.
     *
     * @param[in] code Template bytes.
     * @param[in] patches The holes to fill.
     * @return Offset of the emitted block, or nullopt when it does not fit.
     */
    [[nodiscard]] std::optional<std::size_t> emit_multi(std::span<const std::uint8_t> code,
                                                        std::span<const JitPage::Patch> patches);

    /**
     * @brief Writes @p size bytes at @p offset (before seal()).
     *
     * Used to fill a call target once the block's base address is known.
     *
     * @param[in] offset Byte offset inside the page.
     * @param[in] size Width in bytes (1, 4, or 8).
     * @param[in] value Little-endian value.
     * @return False when the page is sealed or the range overflows.
     */
    [[nodiscard]] bool patch(std::size_t offset, std::size_t size, std::uint64_t value);

    /**
     * @brief Flips the page to RX so the emitted code can run.
     * @return False when mprotect refuses.
     */
    [[nodiscard]] bool seal();

    /**
     * @brief Enters the emitted block at @p offset.
     *
     * The block sees the guest register file pointer in RDI, exactly the
     * contract the templates are written against.
     *
     * @param[in] offset Offset returned by emit().
     * @param[in] regs Guest register file base.
     */
    void invoke(std::size_t offset, std::uint64_t* regs) const;

private:
    std::uint8_t* data_ = nullptr;
    std::size_t used_ = 0;
    bool executable_ = false;
};

/**
 * @brief The template set the skeleton understands today.
 *
 * Each template ends in `ret` and operates on the guest register file
 * through RDI (the pointer invoke() passes).
 */
enum class JitTemplate : std::uint8_t {
    kMovAlImm8,   ///< mov byte [rdi+0], imm8   (guest AL = imm8)
    kAddRaxImm32, ///< add qword [rdi+0], imm32 (guest RAX += imm32)
};

/// @brief The fixed template bytes and their hole contract.
struct TemplateSpec {
    std::span<const std::uint8_t> bytes; ///< Machine code template.
    std::size_t hole_offset = 0;         ///< Where the u64 hole starts.
    std::size_t hole_size = 0;           ///< 1/4/8 bytes patched.
};

/// @brief Looks up @p id's bytes.
[[nodiscard]] TemplateSpec template_spec(JitTemplate id);

} // namespace micro_forge::base
