/// @file jit_diff_cases.hpp
/// @brief Case-table split of test_jit_diff.cpp: builder helpers plus the
///        width/alu case lists consumed by the differential test fixture.

#pragma once

#include "jit_diff_support.hpp"

#include <cstdint>
#include <vector>

namespace jit_diff_cases {

using jit_diff::DiffCase;
using jit_diff::Reg;

/// @brief Builds an indexed-addressing case (load or store, reg operands fixed).
DiffCase indexed_case(const char* name, std::vector<uint8_t> code, bool store, uint64_t base,
                      uint64_t index = 3, Reg index_reg = Reg::kRcx);

/// @brief Builds an add [mem],reg case with the standard 4D 01 94 24 encoding.
DiffCase add_mem_case(const char* name, uint64_t initial, uint64_t source, uint64_t expected,
                      uint64_t flags, uint64_t base = jit_diff::kData + 0x48,
                      std::vector<uint8_t> code = {0x4D, 0x01, 0x94, 0x24, 0xB8, 0xFF, 0xFF, 0xFF});

/// @brief Width-oriented cases (mov/load/store at 1/2/4/8 bytes).
std::vector<DiffCase> width_cases();

/// @brief ALU-family cases (add/sub/and/or/xor/cmp/test/imul/lea forms).
std::vector<DiffCase> alu_cases();

} // namespace jit_diff_cases
