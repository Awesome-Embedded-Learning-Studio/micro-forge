#!/usr/bin/env bash
# wasm engine artifact build (batch 5).
#
# Produces build-wasm/engine.wasm: STANDALONE_WASM, no Emscripten JS
# glue, single file. The exported linear memory is guest physical memory
# from offset 0 (spec 5.4); engine statics and heap sit above the guest
# window via --global-base, and maximum is pinned at 4 GiB so guest
# accesses inside the envelope never need explicit bounds checks.
#
# Prerequisite: cmake -S . -B build has run once (the generated version
# header lives in build/generated).
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
# shellcheck disable=SC1091
source tools/use-emsdk.sh >/dev/null

# Guest RAM comes from the machine description's [wasm] section, so the
# shell recipe and the C++ side cannot disagree. Boot-cost history: the
# kernel zeroes physical memory, and every doubling roughly doubles the
# boot budget before the shell prompt appears.
MACHINE_NAME="${MICRO_FORGE_MACHINE:-cinux}"
GUEST_RAM_MB="$(python3 -c "
import pathlib, tomllib
raw = tomllib.loads(pathlib.Path('machines/${MACHINE_NAME}.toml').read_text())
print(raw['wasm']['guest_ram_mb'])
")"
GUEST_END=$(( GUEST_RAM_MB * 1024 * 1024 + 0x400000 )) # + LFB window
GLOBAL_BASE=$(( GUEST_END + 0x1000000 ))               # + 16 MiB margin
INITIAL_MEMORY=$(( GLOBAL_BASE + 0x1000000 ))          # + heap headroom
OUT="${OUT:-build-wasm/engine.wasm}"
# LLVM must see the hot loop and helpers across translation units. Keep
# an opt-out for controlled comparisons and optional names for V8 profiles.
WASM_LTO="${WASM_LTO:-1}"
WASM_PROFILE="${WASM_PROFILE:-0}"
OPT_FLAGS=()
if [[ "${WASM_LTO}" == "1" ]]; then
    OPT_FLAGS+=(-flto)
fi
if [[ "${WASM_PROFILE}" == "1" ]]; then
    OPT_FLAGS+=(--profiling-funcs)
fi

# Export list derives from the sources (scripts/gen/gen_wasm_exports.py),
# so the ABI surface and the link line cannot drift apart. _malloc/_free
# are the Emscripten allocator entries the standalone module needs.
EXPORTS_FILE="${WASM_EXPORTS_OUT:-build-wasm/exports.list}"
python3 scripts/gen/gen_wasm_exports.py
EXPORTS="$(sed 's/^/_/' "${EXPORTS_FILE}" | paste -sd ',' -),_malloc,_free"

mkdir -p "$(dirname "${OUT}")"
# shellcheck disable=SC2086
em++ -std=c++23 -O3 "${OPT_FLAGS[@]}" -fno-exceptions -fno-rtti \
    -Iinclude -Iexample -Ibuild/generated -Isrc \
    src/machine.cpp src/base/active_machine.cpp src/jit/jit.cpp src/jit/jit_translate*.cpp src/wasm/wasm_emit.cpp src/wasm/wasm_block.cpp src/wasm/wasm_block_mem.cpp src/wasm/wasm_block_walk.cpp src/wasm/wasm_block_stack.cpp \
    example/cinux/cinux_loader.cpp \
    src/kernel/*.cpp src/kernel/*/*.cpp src/arch/x86_64/*.cpp \
    src/wasm/exports.cpp \
    src/wasm/instance_table.cpp \
    src/wasm/bridge.cpp \
    src/wasm/block_runner.cpp \
    -o "${OUT}" \
    -sSTANDALONE_WASM --no-entry \
    -s INITIAL_MEMORY=${INITIAL_MEMORY} \
    -s MAXIMUM_MEMORY=4GB -s ALLOW_MEMORY_GROWTH=1 \
    -s TOTAL_STACK=262144 \
    -s EXPORTED_FUNCTIONS=${EXPORTS} \
    -Wl,--global-base=${GLOBAL_BASE} \
    -Wl,--export-memory \
    -Wl,--growable-table

echo "[build-wasm] ${OUT} ($(stat -c%s "${OUT}") bytes)"
echo "[build-wasm] guest window [0, ${GUEST_END}) ram=${GUEST_RAM_MB}MiB, engine base ${GLOBAL_BASE}"
