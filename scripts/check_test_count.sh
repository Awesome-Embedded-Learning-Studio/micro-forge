#!/usr/bin/env bash
# check_test_count.sh — test-discovery regression floor (COVERAGE-METHODOLOGY §3).
# ============================================================================
# A refactor that breaks `gtest_discover_tests` can silently drop dozens of
# tests with no compile error — the build is green but coverage evaporates.
# This guard runs `ctest -N` (cheap: discovery listing only, no execution) and
# fails if the discovered PRODUCT-test count falls below BASELINE.
#
# It excludes meta-guards (harness, not product tests, and some are host-gated
# like the qemu oracle): this guard itself, oracle_cortex_m3, and future
# coverage gates. Add a new meta-guard? Append its name to the awk exclusion
# below. The floor stays the stable product-test count across hosts. Bump
# BASELINE only when you add a PRODUCT test; never lower it.
#
# Usage: check_test_count.sh [build_dir]   (default: build)
# Exit:  0 = ok   1 = below floor / parse error   77 = skipped (no build dir)
set -euo pipefail

BUILD="${1:-build}"
BASELINE=357   # floor = PRODUCT test count — only ever raised, never lowered (2026-07-08: 353→357, GUI Session tests)

if [[ ! -d "$BUILD" ]]; then
  echo "skip: build dir '$BUILD' not found (run cmake configure first)"
  exit 77
fi

LIST="$(ctest --test-dir "$BUILD" -N 2>/dev/null)" || true
if [[ -z "$LIST" ]]; then
  echo "fail: 'ctest -N' produced no output for '$BUILD'"
  exit 1
fi

# Discovered tests minus meta-guards (harness, host-gated or self-referential).
REAL="$(printf '%s\n' "$LIST" \
        | awk '/Test +#[0-9]+:/ && !/test_count_floor|oracle_cortex_m3/ {c++} END {print c+0}')"

if [[ $REAL -lt $BASELINE ]]; then
  echo "fail: discovered $REAL tests < baseline $BASELINE (tests silently dropped?)"
  exit 1
fi

echo "ok: discovered $REAL tests >= baseline $BASELINE"
