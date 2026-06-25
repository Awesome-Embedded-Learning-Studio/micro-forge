#!/usr/bin/env bash
# prepare_corpus.sh — rebuild the minimal CubeF1 subtree on D:\mf for the
# armcc/AC6 firmware corpus build.
# ============================================================================
# Copies the vendored third_party/STM32CubeF1 minimal subtree to
# /mnt/d/mf/STM32CubeF1 (== D:\mf\STM32CubeF1), PRESERVING the directory depth
# the .uvprojx files expect: each example's includes use `../../../../../../Drivers`
# (6 levels up from Projects/.../<ex>/MDK-ARM to the CubeF1 root where Drivers
# lives). So Drivers/ and Projects/ MUST sit at the top of D:\mf\STM32CubeF1.
#
# Run from WSL (writes to NTFS via /mnt/d, which is writable; only the Keil
# *build* must run native on Windows via build_corpus_opt.ps1 — REGENERATE.md).
#
# CMSIS is trimmed: keep Device + Core + Include (~14M), drop DSP/Lib/docs/NN/
# Core_A/RTOS/RTOS2 (~111M of build-irrelevant bulk). CMSIS Core headers also
# arrive via the D:\MDK-Pack CMSIS pack at build time.
set -euo pipefail

SRC="/home/charliechen/micro-forge/third_party/STM32CubeF1"
DST="/mnt/d/mf/STM32CubeF1"
EXAMPLES=(GPIO/GPIO_IOToggle TIM/TIM_TimeBase UART/UART_Printf)
EXBASE="Projects/STM32F103RB-Nucleo/Examples"

[[ -d "$SRC/Drivers" ]] || { echo "SRC missing: $SRC"; exit 1; }
if [[ -e "$DST" && -n "$(ls -A "$DST" 2>/dev/null)" ]]; then
    echo "DST exists and is non-empty: $DST"; echo "  rm -rf '$DST' first, or pass --force"; [[ "${1:-}" == "--force" ]] && rm -rf "$DST" || exit 1
fi

echo ">> SRC = $SRC"; echo ">> DST = $DST"
mkdir -p "$DST/Drivers/CMSIS" "$DST/$EXBASE"

echo ">> copying Drivers ..."
cp -a "$SRC/Drivers/BSP"               "$DST/Drivers/"
cp -a "$SRC/Drivers/STM32F1xx_HAL_Driver" "$DST/Drivers/"
cp -a "$SRC/Drivers/CMSIS/Device"      "$DST/Drivers/CMSIS/"
cp -a "$SRC/Drivers/CMSIS/Core"        "$DST/Drivers/CMSIS/"
cp -a "$SRC/Drivers/CMSIS/Include"     "$DST/Drivers/CMSIS/"
cp -a "$SRC/Drivers/CMSIS/LICENSE.txt" "$DST/Drivers/CMSIS/" 2>/dev/null || true

echo ">> copying Examples ..."
for ex in "${EXAMPLES[@]}"; do
    mkdir -p "$DST/$EXBASE/$(dirname "$ex")"
    cp -a "$SRC/$EXBASE/$ex" "$DST/$EXBASE/$ex"
done

echo ">> verifying 6-level depth resolves to Drivers ..."
probe="$DST/$EXBASE/GPIO/GPIO_IOToggle/MDK-ARM"
ok=1
for _ in 1 2 3 4 5 6; do probe="$(dirname "$probe")"; done
[[ -d "$probe/Drivers" ]] && echo "   OK: $probe/Drivers exists" || { echo "   FAIL: Drivers not at CubeF1 root"; ok=0; }
[[ -f "$DST/$EXBASE/GPIO/GPIO_IOToggle/MDK-ARM/Project.uvprojx" ]] && echo "   OK: Project.uvprojx present" || { echo "   FAIL: Project.uvprojx missing"; ok=0; }

echo ">> sizes"; du -sh "$DST" "$DST/Drivers" 2>/dev/null
echo ""; echo "Done. Next (on Windows): pwsh test/firmware/armcc/build_corpus_opt.ps1"
[[ $ok -eq 1 ]] || exit 2
