/**
 * @file wasm_emit.cpp
 * @brief Minimal WebAssembly binary emitter.
 * @version 0.1.0
 */

#include "micro_forge/base/wasm_emit.hpp"

#include <array>

namespace micro_forge::base {

namespace {

constexpr std::uint8_t kSectionType = 1;
constexpr std::uint8_t kSectionFunction = 3;
constexpr std::uint8_t kSectionMemory = 5;
constexpr std::uint8_t kSectionExport = 7;
constexpr std::uint8_t kSectionCode = 10;

constexpr std::uint8_t kFuncType = 0x60;
constexpr std::uint8_t kExportFunction = 0x00;
constexpr std::uint8_t kKindFunc = 0x00;
constexpr std::uint8_t kLimitsMin = 0x00;

/// Wraps @p payload in a section header.
void append_section(std::vector<std::uint8_t>& out, std::uint8_t id,
                    const std::vector<std::uint8_t>& payload) {
    if (payload.empty()) {
        return;
    }
    out.push_back(id);
    append_uleb(out, payload.size());
    out.insert(out.end(), payload.begin(), payload.end());
}

} // namespace

void append_uleb(std::vector<std::uint8_t>& out, std::uint64_t value) {
    do {
        auto byte = static_cast<std::uint8_t>(value & 0x7FU);
        value >>= 7U;
        if (value != 0) {
            byte = static_cast<std::uint8_t>(byte | 0x80U);
        }
        out.push_back(byte);
    } while (value != 0);
}

void append_sleb(std::vector<std::uint8_t>& out, std::int64_t value) {
    bool more = true;
    while (more) {
        const auto bits = static_cast<std::uint8_t>(static_cast<std::uint64_t>(value) & 0x7FU);
        auto byte = bits;
        // Arithmetic shift keeps the sign; the bitwise-operand check does
        // not apply to shifts on the signed accumulator.
        // NOLINTNEXTLINE(bugprone-signed-bitwise)
        value >>= 7;
        const bool sign_bit = (static_cast<unsigned>(bits) & 0x40U) != 0U;
        if ((value == 0 && !sign_bit) || (value == -1 && sign_bit)) {
            more = false;
        } else {
            byte = static_cast<std::uint8_t>(byte | 0x80U);
        }
        out.push_back(byte);
    }
}

std::size_t WasmModule::add_function(std::span<const ValType> params,
                                     std::span<const ValType> results,
                                     std::span<const std::uint8_t> body,
                                     std::span<const std::pair<std::uint32_t, ValType>> locals) {
    functions_.push_back(
        Function{.params = params, .results = results, .body = body, .locals = locals});
    return functions_.size() - 1;
}

void WasmModule::add_memory(std::uint32_t pages) {
    memory_pages_ = pages;
}

void WasmModule::export_function(std::string_view name, std::size_t function) {
    exports_.push_back(Export{.name = std::string{name}, .function = function, .is_memory = false});
}

void WasmModule::export_memory(std::string_view name) {
    exports_.push_back(Export{.name = std::string{name}, .function = 0, .is_memory = true});
}

std::vector<std::uint8_t> WasmModule::bytes() const {
    std::vector<std::uint8_t> out = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};

    // Type section: one func type per distinct (params, results) shape.
    std::vector<std::uint8_t> types;
    append_uleb(types, functions_.size());
    for (const Function& function : functions_) {
        types.push_back(kFuncType);
        append_uleb(types, function.params.size());
        for (const ValType type : function.params) {
            types.push_back(static_cast<std::uint8_t>(type));
        }
        append_uleb(types, function.results.size());
        for (const ValType type : function.results) {
            types.push_back(static_cast<std::uint8_t>(type));
        }
    }
    append_section(out, kSectionType, types);

    // Function section: each function points at its type index.
    std::vector<std::uint8_t> funcs;
    append_uleb(funcs, functions_.size());
    for (std::size_t i = 0; i < functions_.size(); ++i) {
        append_uleb(funcs, i);
    }
    append_section(out, kSectionFunction, funcs);

    if (memory_pages_ > 0) {
        std::vector<std::uint8_t> memory;
        append_uleb(memory, 1);
        memory.push_back(kLimitsMin);
        append_uleb(memory, memory_pages_);
        append_section(out, kSectionMemory, memory);
    }

    if (!exports_.empty()) {
        std::vector<std::uint8_t> exports;
        append_uleb(exports, exports_.size());
        for (const Export& entry : exports_) {
            append_uleb(exports, entry.name.size());
            exports.insert(exports.end(), entry.name.begin(), entry.name.end());
            if (entry.is_memory) {
                exports.push_back(0x02); // memory
                exports.push_back(0x00); // memory index
            } else {
                exports.push_back(kExportFunction);
                append_uleb(exports, entry.function);
            }
        }
        append_section(out, kSectionExport, exports);
    }

    // Code section: locals vector + body for each function.
    std::vector<std::uint8_t> code;
    append_uleb(code, functions_.size());
    for (const Function& function : functions_) {
        std::vector<std::uint8_t> entry;
        append_uleb(entry, function.locals.size());
        for (const auto& [count, type] : function.locals) {
            append_uleb(entry, count);
            entry.push_back(static_cast<std::uint8_t>(type));
        }
        entry.insert(entry.end(), function.body.begin(), function.body.end());
        entry.push_back(0x0B); // end of body
        append_uleb(code, entry.size());
        code.insert(code.end(), entry.begin(), entry.end());
    }
    append_section(out, kSectionCode, code);

    return out;
}

} // namespace micro_forge::base
