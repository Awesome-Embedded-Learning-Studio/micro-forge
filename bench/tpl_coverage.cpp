/// @file tpl_coverage.cpp
/// @brief Translation-coverage audit: feed every differential case through
///        translate_insn (no execution) and diff the chosen-template set
///        against the template registry. Zero-hit templates are blind spots
///        no differential case reaches; the kPushRegHelper [rax] bug hid in
///        exactly such a gap.
#include <micro_forge/arch/x86_64/decoder.hpp>

#include <algorithm>
#include <print>
#include <span>
#include <string>
#include <vector>

#include <micro_forge/base/jit_templates_generated.hpp>
#include <micro_forge/base/jit_translate.hpp>

#include "jit_diff_cases.hpp"

namespace {

/// @brief One differential case plus its source table, for reporting.
struct SourcedCase {
    const char* table;
    jit_diff::DiffCase case_data;
};

/// @brief Collects every differential case from the three case tables.
std::vector<SourcedCase> collect_cases() {
    std::vector<SourcedCase> all;
    for (auto& c : jit_diff_cases::base_cases()) {
        all.push_back({.table = "base", .case_data = std::move(c)});
    }
    for (auto& c : jit_diff_cases::width_cases()) {
        all.push_back({.table = "width", .case_data = std::move(c)});
    }
    for (auto& c : jit_diff_cases::alu_cases()) {
        all.push_back({.table = "alu", .case_data = std::move(c)});
    }
    for (auto& c : jit_diff_cases::extra_cases()) {
        all.push_back({.table = "extra", .case_data = std::move(c)});
    }
    return all;
}

} // namespace

// NOLINTNEXTLINE(bugprone-exception-escape) -- bench tool, aborts on OOM
int main() {
    using micro_forge::base::jit_gen::kTemplateRegistry;
    const std::span<const micro_forge::base::jit_gen::TemplateEntry> registry(kTemplateRegistry);
    std::vector<std::uint64_t> hits(registry.size(), 0);

    std::size_t decoded = 0;
    std::size_t translated = 0;
    std::size_t interpreter_only = 0;
    std::size_t decode_failed = 0;
    for (const auto& sourced : collect_cases()) {
        const auto& code = sourced.case_data.code;
        const auto insn = micro_forge::arch::x86_64::decode_one(code);
        if (!insn.has_value()) {
            ++decode_failed;
            std::println(stderr, "tplcov: decode failed case={}/{}", sourced.table,
                         sourced.case_data.name);
            continue;
        }
        ++decoded;
        const auto lowered = micro_forge::base::translate_insn(*insn, 0);
        if (!lowered.has_value()) {
            if (sourced.case_data.require_lowered) {
                std::println(stderr, "tplcov: expected lowered case={}/{}", sourced.table,
                             sourced.case_data.name);
            }
            ++interpreter_only;
            continue;
        }
        ++translated;
        const auto* data = lowered->code.data();
        for (std::size_t i = 0; i < hits.size(); ++i) {
            if (registry[i].data == data) {
                ++hits[i];
                break;
            }
        }
    }

    std::size_t used = 0;
    for (const auto count : hits) {
        used += count > 0 ? 1 : 0;
    }
    std::println(stderr, "tplcov cases: decoded={} translated={} interp={} decode-failed={}",
                 decoded, translated, interpreter_only, decode_failed);
    std::println(stderr, "tplcov templates: used={}/{} zero-hit={}", used, hits.size(),
                 hits.size() - used);
    for (std::size_t i = 0; i < hits.size(); ++i) {
        std::println("{:>6} {}", hits[i], registry[i].name);
    }
    return decode_failed == 0 ? 0 : 1;
}
