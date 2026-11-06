#!/usr/bin/env bash
# micro-forge acceptance gate: one command, all thresholds.
# Borrowed shape from CinuxOS scripts/checks/acceptance.sh (segments,
# per-segment logs, final verdict). Sanitizer cells join when batch 3
# brings the executor and perf baselines.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../.."
logs="${MF_CHECK_LOG_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/mf-acceptance-XXXXXX")}"
mkdir -p "$logs"

run_segment() {
    local name="$1"
    shift
    echo "[acceptance] RUN ${name}"
    echo "RUNNING" >"${logs}/${name}.status"
    if ! "$@" >"${logs}/${name}.log" 2>&1; then
        echo "FAILED" >"${logs}/${name}.status"
        return 1
    fi
    echo "OK" >"${logs}/${name}.status"
}

failed=0
segments=()

# Build: configure + compile the default tree (compile options carry -Werror).
segments+=(build)
run_segment build bash -c \
    'cmake -S . -B build >/dev/null && cmake --build build -j"$(nproc)"' \
    || failed=1

# Tests: full ctest (unit, differential corpus, bare client, line-limit gate).
segments+=(test)
run_segment test bash -c 'ctest --test-dir build --output-on-failure' || failed=1

# Format: clang-format over tracked C++ (Doxygen banner style intact).
segments+=(format)
run_segment format bash -c \
    'clang-format --dry-run --Werror $(find include src tools tests -name "*.cpp" -o -name "*.hpp")' \
    || failed=1

# Doxygen: header comment discipline (batch-1 gate, restored per CR).
segments+=(doxygen)
run_segment doxygen python3 scripts/doxygen/lint.py || failed=1

# Tidy: clang-tidy over the tracked sources (src + tests + tools, the
# batch-1 zero-warning bar). --quiet reports warnings only.
segments+=(tidy)
run_segment tidy bash -c \
    'clang-tidy -p build --quiet $(find src tools tests -name "*.cpp" -o -name "*.hpp")' \
    || failed=1

# Differential vs objdump: the authority gate. Two tiers: the full guest
# kernel image when MF_KERNEL_BIN points at one (a sibling project's build
# artifact; no machine-local default), always backed by the in-repo 64 KiB
# sample so the gate never silently skips.
segments+=(differential)
segments+=(differential_sample)
if [[ -n "${MF_KERNEL_BIN:-}" && -f "${MF_KERNEL_BIN}" ]]; then
    run_segment differential python3 scripts/dec/differential.py \
        "${MF_KERNEL_BIN}" || failed=1
else
    echo "SKIP" >"${logs}/differential.status"
    echo "[acceptance] differential: SKIP (MF_KERNEL_BIN unset or missing)"
fi
run_segment differential_sample python3 scripts/dec/differential.py \
    tests/data/kernel-sample.bin || failed=1

for name in "${segments[@]}"; do
    status="$(cat "${logs}/${name}.status" 2>/dev/null || echo FAILED)"
    if [[ "${status}" != "OK" && "${status}" != "SKIP" ]]; then
        echo "[acceptance] ${name}: FAILED — full log follows"
        cat "${logs}/${name}.log"
    else
        echo "[acceptance] ${name}: ${status}"
    fi
done

if [[ "${failed}" -ne 0 ]]; then
    echo "[acceptance] verdict: FAILED (logs kept in ${logs})"
    exit 1
fi
if [[ -z "${MF_CHECK_LOG_DIR:-}" ]]; then
    rm -rf "$logs"
else
    echo "[acceptance] logs kept in ${logs}"
fi
echo "[acceptance] verdict: build, tests, format, doxygen, tidy, differential green"
