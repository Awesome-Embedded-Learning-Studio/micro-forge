#!/usr/bin/env bash
# Emscripten toolchain bootstrap (batch 5, wasm engine artifact).
#
# emsdk is a TOOLCHAIN (same category as gcc/clang), not a build
# dependency: it is not linked into any artifact and carries no license
# surface into the product. It lives inside the working tree only so the
# sources stay browsable in the editor; the clone is gitignored
# (tools/emsdk/) and the version is pinned right here. googletest remains
# the only third-party BUILD dependency (FetchContent, see tests/).
#
# Usage:
#   tools/use-emsdk.sh install    # one-time: clone + install (~2-3 GB)
#   tools/use-emsdk.sh            # activate; errors when not installed
#   source tools/use-emsdk.sh     # activate and export env (emcc on PATH)
#
# The acceptance gate only ever activates: installing a toolchain is not
# part of a gate run, and the wasm segment records SKIP when the
# toolchain is absent (same stance as the full-kernel differential).
set -euo pipefail

# Pinned toolchain version. Started as "latest" and backfilled with the
# concrete version after the first install so every machine and CI run
# fetches byte-identical compilers.
EMSDK_VERSION="7.0.0"  # pinned 2026-11-07 (first install; release 6afbd834)

EMSDK_HOME="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/emsdk"

if [[ "${1:-activate}" == "install" ]]; then
    if [[ ! -d "$EMSDK_HOME" ]]; then
        echo "[use-emsdk] cloning emsdk into ${EMSDK_HOME} (gitignored)"
        git clone --depth 1 https://github.com/emscripten-core/emsdk.git "$EMSDK_HOME"
    fi
    (
        cd "$EMSDK_HOME"
        ./emsdk install "$EMSDK_VERSION"
        ./emsdk activate "$EMSDK_VERSION"
    )
fi

if [[ ! -x "$EMSDK_HOME/emsdk" ]]; then
    echo "[use-emsdk] no toolchain at ${EMSDK_HOME}; run: tools/use-emsdk.sh install" >&2
    exit 1
fi

# Export for the caller when sourced; harmless when executed.
# shellcheck disable=SC1091
source "$EMSDK_HOME/emsdk_env.sh" >/dev/null

echo "[use-emsdk] emcc: $(command -v emcc)"
emcc --version | head -1
