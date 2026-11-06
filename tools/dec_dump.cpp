/**
 * @file dec_dump.cpp
 * @brief Decode-dump tool: linear decode of a binary, one line per
 *        instruction, plus a whitelist-coverage histogram. Feeds the
 *        objdump differential harness.
 * @version 0.1.0
 */

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <print>
#include <span>
#include <string>
#include <vector>

#include "micro_forge/arch/x86_64/decoder.hpp"

namespace {

int run_dump(const std::vector<uint8_t>& bytes, uint64_t start) {
    using namespace micro_forge::arch::x86_64;
    uint64_t off = start;
    while (off < bytes.size()) {
        const auto r = decode_one(std::span<const uint8_t>(bytes).subspan(off));
        if (!r) {
            std::println("{:x}\tFAIL\t{}", static_cast<unsigned long long>(off),
                         static_cast<int>(r.error().kind));
            // Linear scan: skip one byte and retry (whitelist gaps must not
            // stop the dump).
            ++off;
            continue;
        }
        std::string const text = to_text(*r, off);
        std::println("{:x}\t{}\t{}", static_cast<unsigned long long>(off), r->length, text);
        off += (r->length != 0U) ? r->length : 1;
    }
    return 0;
}

int run_stats(const std::vector<uint8_t>& bytes, uint64_t start) {
    using namespace micro_forge::arch::x86_64;
    std::map<std::string, uint64_t> hist;
    uint64_t ok = 0;
    uint64_t fail = 0;
    uint64_t off = start;
    std::vector<uint64_t> fail_offsets;
    std::map<int, uint64_t> fail_kinds;
    while (off < bytes.size()) {
        const auto r = decode_one(std::span<const uint8_t>(bytes).subspan(off));
        if (!r) {
            ++fail;
            fail_offsets.push_back(off);
            ++fail_kinds[static_cast<int>(r.error().kind)];
            ++off;
            continue;
        }
        ++ok;
        ++hist[std::string(mnemonic_name(r->mnemonic))];
        off += (r->length != 0U) ? r->length : 1;
    }
    std::println("decoded_insn_count {}", static_cast<unsigned long long>(ok));
    std::println("fail_site_count {}", static_cast<unsigned long long>(fail));
    for (const auto& [kind, n] : fail_kinds) {
        std::println("fail_kind_{} {}", kind, static_cast<unsigned long long>(n));
    }
    std::println("-- histogram --");
    for (const auto& [name, n] : hist) {
        std::println("{}\t{}", static_cast<unsigned long long>(n), name);
    }
    std::println("-- first 40 fail offsets --");
    for (size_t i = 0; i < fail_offsets.size() && i < 40; ++i) {
        std::println("{:x}", static_cast<unsigned long long>(fail_offsets[i]));
    }
    return 0;
}

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char* const* argv) {
    if (argc < 2) {
        std::println(stderr, "usage: mf-decode-dump [--stats] <bin> [start-offset]");
        return 2;
    }
    bool stats = false;
    const char* path = nullptr;
    uint64_t start = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--stats") {
            stats = true;
        } else if (path == nullptr) {
            path = argv[i];
        } else {
            start = std::strtoull(argv[i], nullptr, 0);
        }
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::println(stderr, "cannot open {}", path);
        return 2;
    }
    std::vector<uint8_t> const bytes((std::istreambuf_iterator<char>(f)),
                                     std::istreambuf_iterator<char>());
    return stats ? run_stats(bytes, start) : run_dump(bytes, start);
}
