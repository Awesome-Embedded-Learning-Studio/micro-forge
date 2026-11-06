/**
 * @file test_bare_client.cpp
 * @brief Batch-2 acceptance: the GNU-as bare client must decode 100%.
 * @version 0.1.0
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <map>
#include <vector>

#include "micro_forge/arch/x86_64/decoder.hpp"

namespace {

/// @brief Path injected by CMake (BARE_CLIENT_BIN).
std::vector<uint8_t> load_client() {
    std::ifstream f(BARE_CLIENT_BIN, std::ios::binary);
    EXPECT_TRUE(f.good()) << "cannot open " << BARE_CLIENT_BIN;
    return {(std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()};
}

TEST(BareClientTest, DecodesFullyWithoutFailures) {
    const auto bytes = load_client();
    ASSERT_FALSE(bytes.empty());
    uint64_t off = 0;
    uint64_t count = 0;
    while (off < bytes.size()) {
        const auto r =
            micro_forge::arch::x86_64::decode_one(std::span<const uint8_t>(bytes).subspan(off));
        ASSERT_TRUE(r.has_value()) << "decode failed at " << std::hex << off << std::dec
                                   << " kind=" << static_cast<int>(r.error().kind);
        ASSERT_GT(r->length, 0U);
        off += r->length;
        ++count;
    }
    // The corpus covers ALU, branches, call/ret, memory, push/pop,
    // syscall: expect a real instruction population, not a stray byte run.
    EXPECT_GE(count, 40U);
}

} // namespace
