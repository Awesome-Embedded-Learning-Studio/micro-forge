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

# Guest RAM: keep 256 MiB. The kernel zeroes physical memory during
# boot; 256 MiB costs about 2e8 instructions, while 512 MiB doubles the
# boot budget and pushes the shell prompt past 4e8 (the demo budget).
GUEST_RAM_MB="${GUEST_RAM_MB:-256}"
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

EXPORTS="_mf_abi_version,_mf_engine_version,_mf_snapshot_format_version"
EXPORTS="${EXPORTS},_mf_guest_ram_size,_mf_lfb_base,_mf_lfb_size"
EXPORTS="${EXPORTS},_mf_reset,_mf_run,_mf_icount"
EXPORTS="${EXPORTS},_mf_read_console,_mf_snapshot,_mf_restore"
EXPORTS="${EXPORTS},_mf_boot_cinux,_mf_inject_serial,_malloc,_free"
EXPORTS="${EXPORTS},_mf_rip_low,_mf_rip_high,_mf_peek32,_mf_serial_tx"
EXPORTS="${EXPORTS},_mf_instance_create,_mf_instance_destroy,_mf_instance_use"
EXPORTS="${EXPORTS},_mf_wasm_block,_mf_wasm_block2,_mf_gprs_flush,_mf_gprs_commit,_mf_bridge_cpu_state,_mf_bridge_prepare,_mf_bridge_finish,_mf_bridge_exit,_mf_gpr_bridge_base,_mf_fetch_peek,_mf_write_guest"
EXPORTS="${EXPORTS},_mf_gpr_get,_mf_gpr_set,_mf_set_rip"
EXPORTS="${EXPORTS},_mf_flags_get,_mf_flags_set"
EXPORTS="${EXPORTS},_mf_retire"
EXPORTS="${EXPORTS},_mf_event_budget"
EXPORTS="${EXPORTS},_mf_note_store,_mf_code_epoch,_mf_tlb_epoch"
EXPORTS="${EXPORTS},_mf_runner_reset,_mf_runner_put,_mf_runner_refuse,_mf_runner_remove,_mf_runner_reepoch,_mf_runner_set_batch,_mf_runner_run,_mf_runner_dump"
EXPORTS="${EXPORTS},_mf_runner_retired,_mf_runner_exit,_mf_runner_miss_lo,_mf_runner_miss_hi,_mf_runner_execs,_mf_runner_handoffs,_mf_runner_partial_handoffs,_mf_runner_reason,_mf_runner_block_retired,_mf_runner_interp_retired,_mf_runner_fallback_cause"

mkdir -p "$(dirname "${OUT}")"
# shellcheck disable=SC2086
em++ -std=c++23 -O3 "${OPT_FLAGS[@]}" -fno-exceptions -fno-rtti \
    -DGUEST_RAM_MB=${GUEST_RAM_MB} \
    -Iinclude -Ibuild/generated -Isrc \
    src/machine.cpp src/engine.cpp src/base/jit.cpp src/base/jit_translate*.cpp src/base/wasm_emit.cpp src/base/wasm_block.cpp src/base/wasm_block_mem.cpp src/base/wasm_block_walk.cpp src/presets/cinux/cinux_machine.cpp src/kernel/*.cpp src/kernel/*/*.cpp src/arch/x86_64/*.cpp \
    src/loader/*.cpp \
    src/wasm/exports.cpp \
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
