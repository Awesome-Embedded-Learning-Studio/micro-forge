#!/usr/bin/env bash
# check_test_count.sh — test-discovery regression floor (COVERAGE-METHODOLOGY §3).
# ============================================================================
# A refactor that breaks `gtest_discover_tests` can silently drop dozens of
# tests with no compile error — the build is green but coverage evaporates.
# This guard runs `ctest -N` (cheap: discovery listing only, no execution) and
# fails if the discovered count falls below BASELINE.
#
# It excludes ITSELF from the count (it is a meta-guard, not a product test).
# Real tests — including later coverage guards (oracle differential, gcov) —
# count toward the floor. Bump BASELINE when you add tests; never lower it.
#
# Assumption: BASELINE reflects a host with the full corpus present (incl. the
# optional e2e / armcc-firmware tests). CI registration is deferred
# (COVERAGE-METHODOLOGY decision #1), so this is a local-dev guard for now.
#
# Usage: check_test_count.sh [build_dir]   (default: build)
# Exit:  0 = ok   1 = below floor / parse error   77 = skipped (no build dir)
set -euo pipefail

BUILD="${1:-build}"
BASELINE=321   # floor — only ever raised, never lowered

if [[ ! -d "$BUILD" ]]; then
  echo "skip: build dir '$BUILD' not found (run cmake configure first)"
  exit 77
fi

LIST="$(ctest --test-dir "$BUILD" -N 2>/dev/null)" || true
if [[ -z "$LIST" ]]; then
  echo "fail: 'ctest -N' produced no output for '$BUILD'"
  exit 1
fi

# Discovered tests minus this meta-guard itself (ctest lists it as
# "Test #N: test_count_floor").
REAL="$(printf '%s\n' "$LIST" \
        | awk '/Test +#[0-9]+:/ && !/test_count_floor/ {c++} END {print c+0}')"

if [[ $REAL -lt $BASELINE ]]; then
  echo "fail: discovered $REAL tests < baseline $BASELINE (tests silently dropped?)"
  exit 1
fi

echo "ok: discovered $REAL tests >= baseline $BASELINE"
