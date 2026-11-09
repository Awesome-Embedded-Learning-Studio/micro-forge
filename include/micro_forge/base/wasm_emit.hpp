/**
 * @file wasm_emit.hpp
 * @brief Minimal WebAssembly binary emitter for the JIT backend.
 * @version 0.1.0
 *
 * The second JIT backend (plan B): guest basic blocks become wasm functions
 * that the page loads with WebAssembly.compile. This emitter covers the
 * sections a code block needs: type, function, memory, export, code.
 * It is deliberately tiny and append-only; anything fancier waits for a
 * real need.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace micro_forge::base {

/**
 * @brief Appends a ULEB128 value to @p out.
 * @param[in,out] out Destination bytes.
 * @param[in] value Value to encode.
 */
void append_uleb(std::vector<std::uint8_t>& out, std::uint64_t value);

/**
 * @brief Appends a SLEB128 value to @p out.
 * @param[in,out] out Destination bytes.
 * @param[in] value Value to encode.
 */
void append_sleb(std::vector<std::uint8_t>& out, std::int64_t value);

/**
 * @brief Builds one wasm module with linear memory and exported functions.
 *
 * Every function the builder learns shares one signature family: i64
 * parameters and results are encoded as the caller supplies; the JIT
 * backend uses (memory_base: i32, ...) signatures.
 */
class WasmModule {
public:
    /// @brief Wasm value type codes.
    enum class ValType : std::uint8_t {
        kI32 = 0x7F, ///< 32-bit integer.
        kI64 = 0x7E, ///< 64-bit integer.
        kF32 = 0x7D, ///< 32-bit float.
        kF64 = 0x7C, ///< 64-bit float.
    };

    /**
     * @brief Declares one function body.
     *
     * @param[in] params Value types of the parameters.
     * @param[in] results Value types of the results.
     * @param[in] body Function body bytes (locals count + instructions,
     *            without the trailing end).
     * @param[in] locals Local declarations as (count, type) pairs.
     * @return The function index.
     */
    std::size_t add_function(std::span<const ValType> params, std::span<const ValType> results,
                             std::span<const std::uint8_t> body,
                             std::span<const std::pair<std::uint32_t, ValType>> locals = {});

    /// @brief Declares one memory of @p pages 64KiB pages.
    /// @param[in] pages Initial page count.
    void add_memory(std::uint32_t pages);

    /**
     * @brief Exports a function under @p name.
     * @param[in] name Export name.
     * @param[in] function Function index from add_function().
     */
    void export_function(std::string_view name, std::size_t function);

    /**
     * @brief Exports the linear memory under @p name.
     * @param[in] name Export name ("memory" is the conventional one).
     */
    void export_memory(std::string_view name);

    /// @brief Serializes the module.
    [[nodiscard]] std::vector<std::uint8_t> bytes() const;

private:
    struct Function {
        std::span<const ValType> params;
        std::span<const ValType> results;
        std::span<const std::uint8_t> body;
        std::span<const std::pair<std::uint32_t, ValType>> locals;
    };
    struct Export {
        std::string name;
        std::size_t function = 0;
        bool is_memory = false;
    };

    std::vector<Function> functions_;
    std::vector<Export> exports_;
    std::uint32_t memory_pages_ = 0;
};

} // namespace micro_forge::base
