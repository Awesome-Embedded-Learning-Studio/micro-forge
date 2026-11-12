#!/usr/bin/env bash
# micro-forge acceptance gate: one command, all thresholds.
# Borrowed shape from CinuxOS scripts/checks/acceptance.sh (segments,
# per-segment logs, final verdict). Sanitizer joined with batch 3 (the
# executor brought pointer-rich guest state worth keeping under ASan/UBSan).
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

# Parallel job budget: a third of the cores, floor 1. Heavy analysis
# processes (clang-tidy, sanitizer compilers) each eat 1-2 GiB; filling
# every core risks OOM on machines that also run an editor and toolchains
# (maintainer guidance, batch 5).
JOBS=$(( $(nproc) / 3 ))
if [ "${JOBS}" -lt 1 ]; then
    JOBS=1
fi
# The segments below run in bash -c children: export or they see "".
export JOBS

# Build: configure + compile the default tree (compile options carry -Werror).
segments+=(build)
run_segment build bash -c \
    'cmake -S . -B build >/dev/null && cmake --build build -j"${JOBS}"' \
    || failed=1

# Tests: full ctest (unit, differential corpus, bare client, line-limit gate).
segments+=(test)
run_segment test bash -c 'ctest --test-dir build --output-on-failure' || failed=1

# Format: clang-format over tracked C++ (Doxygen banner style intact).
segments+=(format)
run_segment format bash -c \
    'clang-format --dry-run --Werror $(git ls-files "*.cpp" "*.hpp")' \
    || failed=1

# Doxygen: header comment discipline (batch-1 gate, restored per CR).
segments+=(doxygen)
run_segment doxygen python3 scripts/doxygen/lint.py || failed=1

# Tidy: clang-tidy over the tracked sources (src + tests + tools + bench,
# the batch-1 zero-warning bar). clang-tidy exits 0 even with warnings
# unless WarningsAsErrors is set, and this repo keeps that field empty for
# editor friendliness -- so the gate has to look at the output itself:
# any "warning:" line fails the segment (hole found during the batch-3
# review, 79 warnings had walked through a green gate).
segments+=(tidy)
run_segment tidy bash -c '
    set -o pipefail
    # One clang-tidy per core over the tracked files: serial runs spend
    # 3-4 minutes re-chewing template-heavy headers per TU; the check
    # only needs the merged warning count. Tool crashes propagate:
    # xargs exits 123 when a child fails, and grep -c exits 1 on zero
    # matches, so the match count is forced true and the pipeline status
    # is consumed explicitly (a crashed tidy with no warning lines must
    # fail the segment, not pass it -- batch-5 review).
    status=0
    git ls-files "*.cpp" "*.hpp" | xargs -r -P "${JOBS}" -n 1 \
        clang-tidy -p build --quiet 2>&1 |
        tee /tmp/mf-acceptance-tidy.log |
        { grep -c "warning:" > /tmp/mf-acceptance-tidy-count || true; } || status=$?
    if [ "${status}" != "0" ]; then
        echo "tidy: analysis tool failed (exit ${status})"
        exit 1
    fi
    count=$(cat /tmp/mf-acceptance-tidy-count)
    if [ "${count}" != "0" ]; then
        echo "tidy: ${count} warnings (gate: zero)"
        exit 1
    fi' || failed=1

# Sanitizer: separate ASan+UBSan tree, full ctest. -fno-sanitize-recover
# makes UBSan abort like ASan so the gate actually fails on the first
# undefined behavior instead of printing past it.
segments+=(sanitizer)
run_segment sanitizer bash -c \
    'cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer" >/dev/null && \
     cmake --build build-san -j"${JOBS}" >/dev/null && \
     ctest --test-dir build-san --output-on-failure' || failed=1

# wasm: the cross artifact (STANDALONE_WASM engine + Node self-test).
# The toolchain is machine-local and activation-only here: installing a
# compiler is not a gate step. The gate fires only when activation
# actually puts emcc on PATH; anything else records SKIP (same stance
# as the full-kernel differential).
segments+=(wasm)
if command -v node >/dev/null 2>&1 \
    && bash -c 'source tools/use-emsdk.sh >/dev/null 2>&1 && command -v emcc >/dev/null 2>&1'; then
    run_segment wasm bash -c \
        'source tools/use-emsdk.sh >/dev/null && scripts/build-wasm.sh && \
         node wasm/harness/selftest.mjs' || failed=1
else
    echo "SKIP" >"${logs}/wasm.status"
    echo "[acceptance] wasm: SKIP (emcc or node missing)"
fi

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
    tests/arch/x86_64/kernel-sample.bin || failed=1

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
echo "[acceptance] verdict: build, tests, format, doxygen, tidy, sanitizer, differential green"
