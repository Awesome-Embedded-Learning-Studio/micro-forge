#include <cstdint>
#include <expected>
#include <string>

#include <gtest/gtest.h>
#include <micro_forge/version.hpp>

namespace {

// TEST_EXPECTED_VERSION is a compile definition taken from the CMake project
// VERSION; kVersion comes from the configure_file-generated header. Asserting
// them against each other guards the whole generation chain, not a constant.
TEST(Smoke, VersionMatchesProject) {
    EXPECT_EQ(micro_forge::kVersion, std::string_view(TEST_EXPECTED_VERSION));
}

// Numeric components and the display string are generated from the same
// project VERSION; cross-checking them catches a template edit that lets the
// two forms drift apart.
TEST(Smoke, VersionComponentsMatchString) {
    const std::string composed = std::to_string(micro_forge::kVersionMajor) + "." +
                                 std::to_string(micro_forge::kVersionMinor) + "." +
                                 std::to_string(micro_forge::kVersionPatch);
    EXPECT_EQ(micro_forge::kVersion, composed);
}

// First proof that the C++23 error channel works in this toolchain:
// std::expected is usable in constant evaluation, the pattern every later
// kernel API will follow.
TEST(Smoke, ExpectedErrorChannelWorks) {
    enum class Status : std::uint8_t { kNotFound };
    constexpr auto ok = std::expected<int, Status>{42};
    static_assert(ok.has_value());
    static_assert(ok.value() == 42);

    constexpr auto err = std::expected<int, Status>{std::unexpected(Status::kNotFound)};
    static_assert(!err.has_value());
    static_assert(err.error() == Status::kNotFound);
}

} // namespace
