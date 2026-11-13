/// @file test_wasm_block.cpp
/// @brief Guest block to wasm body: bytes and a compile-ready module.

#include <gtest/gtest.h>

#include <micro_forge/arch/x86_64/decoder.hpp>
#include <micro_forge/base/wasm_block.hpp>
#include <micro_forge/base/wasm_emit.hpp>

#include <array>
#include <vector>

namespace {

using micro_forge::base::WasmModule;

TEST(WasmBlockTest, LowersMovAddRet) {
    // Guest: mov rax,1 ; add rax,2 ; ret
    static constexpr std::array<std::uint8_t, 12> guest = {
        0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC0, 0x02, 0xC3,
    };
    std::vector<micro_forge::arch::x86_64::DecodedInsn> insns;
    std::size_t pc = 0;
    while (pc < guest.size()) {
        const auto decoded = micro_forge::arch::x86_64::decode_one(
            std::span<const std::uint8_t>(guest.data() + pc, guest.size() - pc));
        ASSERT_TRUE(decoded.has_value());
        insns.push_back(*decoded);
        pc += decoded->length;
    }
    std::vector<std::uint8_t> body;
    const auto lowered = micro_forge::base::append_wasm_block(insns, 0x1000, body);
    EXPECT_EQ(insns.size(), lowered);
    EXPECT_FALSE(body.empty());

    // Build a module exporting the block as a () -> i64 function holding
    // one i64 local per guest GPR (the register bridge).
    WasmModule module;
    module.add_memory(1);
    static constexpr std::array<WasmModule::ValType, 0> no_params = {};
    static constexpr std::array<WasmModule::ValType, 1> rip_result = {
        WasmModule::ValType::kI64,
    };
    using LocalDecl = std::pair<std::uint32_t, WasmModule::ValType>;
    static constexpr std::array<LocalDecl, 3> gpr_locals = {
        LocalDecl{16U, WasmModule::ValType::kI64},
        LocalDecl{2U, WasmModule::ValType::kI64},
        LocalDecl{1U, WasmModule::ValType::kI32},
    };
    const auto index = module.add_function(no_params, rip_result, body, gpr_locals);
    module.export_function("block", index);
    module.export_memory("memory");
    const auto bytes = module.bytes();
    EXPECT_GT(bytes.size(), 40U);
}

} // namespace
