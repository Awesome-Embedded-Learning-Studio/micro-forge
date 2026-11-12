/// @file test_wasm_emit.cpp
/// @brief The wasm emitter: bytes match the hand-written reference shape.

#include <gtest/gtest.h>

#include <micro_forge/base/wasm_emit.hpp>

#include <array>

namespace {

using micro_forge::base::WasmModule;

TEST(WasmEmitTest, AddFunctionMatchesHandWrittenBytes) {
    // (func (param i32 i32) (result i32) local.get 0 local.get 1 i32.add)
    // body without the trailing end; the emitter appends 0x0B.
    static constexpr std::array<std::uint8_t, 5> body = {
        0x20, 0x00, 0x20, 0x01, 0x6A,
    };
    static constexpr std::array<WasmModule::ValType, 2> params = {
        WasmModule::ValType::kI32,
        WasmModule::ValType::kI32,
    };
    static constexpr std::array<WasmModule::ValType, 1> results = {WasmModule::ValType::kI32};
    WasmModule module;
    const auto index = module.add_function(params, results, body);
    module.export_function("add", index);
    const auto bytes = module.bytes();
    // Magic + version, then the type section id and a plausible layout.
    EXPECT_EQ(0x00, bytes[0]);
    EXPECT_EQ(0x61, bytes[1]);
    EXPECT_EQ(0x73, bytes[2]);
    EXPECT_EQ(0x6D, bytes[3]);
    EXPECT_EQ(0x01, bytes[4]);
    // Exactly the reference encoding of this module (41 bytes).
    static constexpr std::array<std::uint8_t, 41> reference = {
        0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00, 0x01, 0x07, 0x01, 0x60, 0x02, 0x7F,
        0x7F, 0x01, 0x7F, 0x03, 0x02, 0x01, 0x00, 0x07, 0x07, 0x01, 0x03, 0x61, 0x64, 0x64,
        0x00, 0x00, 0x0A, 0x09, 0x01, 0x07, 0x00, 0x20, 0x00, 0x20, 0x01, 0x6A, 0x0B,
    };
    ASSERT_EQ(reference.size(), bytes.size());
    for (std::size_t i = 0; i < reference.size(); ++i) {
        EXPECT_EQ(reference[i], bytes[i]) << "byte " << i;
    }
}

TEST(WasmEmitTest, ExportsMemory) {
    WasmModule module;
    module.add_memory(1);
    module.export_memory("memory");
    const auto bytes = module.bytes();
    const std::string text(bytes.begin(), bytes.end());
    EXPECT_NE(std::string::npos, text.find("memory"));
}

TEST(WasmEmitTest, UlebAndSlebRoundTrip) {
    std::vector<std::uint8_t> out;
    micro_forge::base::append_uleb(out, 624485);
    ASSERT_EQ(3U, out.size());
    EXPECT_EQ(0xE5, out[0]);
    EXPECT_EQ(0x8E, out[1]);
    EXPECT_EQ(0x26, out[2]);
    out.clear();
    micro_forge::base::append_sleb(out, -1);
    ASSERT_EQ(1U, out.size());
    EXPECT_EQ(0x7F, out[0]);
}

} // namespace
