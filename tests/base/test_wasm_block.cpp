/// @file test_wasm_block.cpp
/// @brief Guest block to wasm body: bytes and a compile-ready module.

#include <gtest/gtest.h>

#include <micro_forge/arch/x86_64/decoder.hpp>
#include <micro_forge/base/wasm_block.hpp>
#include <micro_forge/base/wasm_emit.hpp>

#include <array>
#include <cstdio>
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
    const auto info = micro_forge::base::append_wasm_block(insns, 0x1000, body);
    EXPECT_EQ(insns.size(), info.lowered);
    // ret now pops its target inside the block instead of handing back
    EXPECT_EQ(micro_forge::base::WasmTerm::kIndirect, info.term);
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
        LocalDecl{9U, WasmModule::ValType::kI64},
        LocalDecl{1U, WasmModule::ValType::kI32},
    };
    const auto index = module.add_function(no_params, rip_result, body, gpr_locals);
    module.export_function("block", index);
    module.export_memory("memory");
    const auto bytes = module.bytes();
    EXPECT_GT(bytes.size(), 40U);
}

TEST(WasmBlockTest, LowersCmpJccBranch) {
    // Guest: cmp rax,0 ; jne +4 ; add rdx,3 (past the branch)
    static constexpr std::array<std::uint8_t, 10> guest = {
        0x48, 0x83, 0xF8, 0x00, 0x75, 0x04, 0x48, 0x83, 0xC2, 0x03,
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
    ASSERT_EQ(std::size_t{3}, insns.size());
    std::vector<std::uint8_t> body;
    const auto info = micro_forge::base::append_wasm_block(insns, 0x1000, body);
    // A jcc no longer ends the block: the taken side exits mid-block with
    // its own instruction count, so all three instructions lower and the
    // block falls through to 0x100A.
    EXPECT_EQ(std::size_t{3}, info.lowered);
    EXPECT_EQ(micro_forge::base::WasmTerm::kNext, info.term);
    EXPECT_TRUE(info.flags_used);
    EXPECT_TRUE(info.flags_dirty);
    EXPECT_EQ(5U, info.used_mask);  // rax (slot 0) + rdx (slot 2)
    EXPECT_EQ(4U, info.dirty_mask); // add rdx,3 (slot 2)
    EXPECT_FALSE(body.empty());
}

TEST(WasmBlockTest, LowersMemLoadStoreForms) {
    // Guest: mov esi,[rax] ; mov [rax+rcx*4],esi ; cmp edx,[rdi+0x3C]
    static constexpr std::array<std::uint8_t, 9> guest = {
        0x8B, 0x30, 0x89, 0x34, 0x88, 0x3B, 0x57, 0x3C, 0xC3,
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
    const auto info = micro_forge::base::append_wasm_block(insns, 0x1000, body);
    // The three memory forms all lower; ret stops the sweep and pops its own
    // target (kIndirect) instead of handing back.
    EXPECT_EQ(std::size_t{4}, info.lowered);
    EXPECT_EQ(micro_forge::base::WasmTerm::kIndirect, info.term);
    EXPECT_TRUE(info.flags_dirty); // cmp writes the flags cell
    // Reads: rax, rcx (store base+index), rdx, rdi, esi (store source) and
    // rsp (ret pops its target through the bridge).
    constexpr auto expect_used = 0xD7U; // rax rcx rdx rdi rsp esi
    EXPECT_EQ(expect_used, info.used_mask);
    EXPECT_EQ(0x50U, info.dirty_mask); // esi (load) and rsp (ret pop)
    EXPECT_FALSE(body.empty());
}

} // namespace
