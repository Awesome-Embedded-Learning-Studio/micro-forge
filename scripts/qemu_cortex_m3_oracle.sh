#!/usr/bin/env bash
# qemu_cortex_m3_oracle.sh — Tier-1 semantic anchor vs QEMU Cortex-M3 (mps2-an385)
# ============================================================================
# Pins the reference semantics micro-forge's unit tests are checked against:
#   * SDIV/UDIV divide semantics (incl. INT_MIN/-1 saturation, /0 => 0)
#   * ADC/SBC shifted-register result + N/Z/C/V flags
#
# Assembles a tiny Thumb program, runs it under qemu-system-arm with a gdbstub,
# breaks at the done loop, dumps the result/xPSR buffers, and COMPARES them to
# the verified-expected values — real exit code (0 = match, 1 = divergence).
#
# This oracle already caught one spec error pre-merge: SDIV/0 returns 0 (not
# INT_MIN) for a negative dividend on Cortex-M3. See document/notes/017.
#
# Toolchain is configurable and the script self-skips (exit 77) when a tool is
# absent, so it is safe to register under ctest on hosts lacking qemu/gdb:
#   ARM_AS (arm-none-eabi-as)  ARM_LD (arm-none-eabi-ld)
#   ARM_GDB (arm-none-eabi-gdb)  QEMU (qemu-system-arm)
#   QEMU_GDB_PORT (12345) — the test is RUN_SERIAL, so the port never collides.
#
# Exit:  0 = QEMU output matches expected   1 = divergence / gdb error
#        77 = skipped (toolchain missing)
set -euo pipefail

# ── Configurable toolchain (skip if any absent) ──
ARM_AS="${ARM_AS:-arm-none-eabi-as}"
ARM_LD="${ARM_LD:-arm-none-eabi-ld}"
ARM_GDB="${ARM_GDB:-arm-none-eabi-gdb}"
QEMU="${QEMU:-qemu-system-arm}"
PORT="${QEMU_GDB_PORT:-12345}"

for t in "$ARM_AS" "$ARM_LD" "$ARM_GDB" "$QEMU"; do
  if ! command -v "$t" >/dev/null 2>&1; then
    echo "skip: required tool '$t' not found on PATH"
    echo "      (override via ARM_AS / ARM_LD / ARM_GDB / QEMU)"
    exit 77
  fi
done

WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
ASM="$WORK/o.s"; LDS="$WORK/o.ld"; ELF="$WORK/o.elf"

# ── Probe program (the part that matters; unchanged, proven) ──
cat > "$ASM" <<'ASM'
.syntax unified
.cpu cortex-m3
.thumb
.section .vectors,"ax",%progbits
.thumb_func
vectors: .word 0x00008000 ; .word Reset_Handler+1
.section .text,"ax",%progbits
.thumb_func
.global done
Reset_Handler:
  ldr  r4, =0x00002000          @ SDIV/UDIV results (4 words)
  ldr  r0, =0xFFFFFFFF ; mov r1,#0 ; sdiv r2,r0,r1 ; str r2,[r4],#4   @ -1/0
  mov  r0,#5 ; sdiv r2,r0,r1 ; str r2,[r4],#4                          @ 5/0
  ldr  r0, =0x80000000 ; ldr r1,=0xFFFFFFFF ; sdiv r2,r0,r1 ; str r2,[r4],#4  @ INT_MIN/-1
  mov  r0,#123 ; mov r1,#0 ; udiv r2,r0,r1 ; str r2,[r4],#4            @ udiv 123/0
  ldr  r6, =0x00002010          @ ADC/SBC results
  ldr  r7, =0x00002020          @ xPSR snapshots (N=31 Z=30 C=29 V=28)
  ldr  r5, =0x20000000 ; msr apsr_nzcvq, r5                            @ C=1
  ldr  r0, =0xFFFFFFFF ; mov r1,#0 ; adcs.w r2,r0,r1 ; str r2,[r6],#4 ; mrs r3,apsr ; str r3,[r7],#4
  mov  r5,#0 ; msr apsr_nzcvq, r5                                      @ C=0
  ldr  r0, =0x7FFFFFFF ; mov r1,#1 ; adcs.w r2,r0,r1 ; str r2,[r6],#4 ; mrs r3,apsr ; str r3,[r7],#4
  ldr  r5, =0x20000000 ; msr apsr_nzcvq, r5                            @ C=1
  mov  r0,#5 ; mov r1,#1 ; sbcs.w r2,r0,r1 ; str r2,[r6],#4 ; mrs r3,apsr ; str r3,[r7],#4
  mov  r0,#5 ; mov r1,#7 ; sbcs.w r2,r0,r1 ; str r2,[r6],#4 ; mrs r3,apsr ; str r3,[r7],#4
.thumb_func
done: b done
ASM

cat > "$LDS" <<'LDS'
SECTIONS {
  . = 0x0;
  .vectors : { *(.vectors) }
  .text    : { *(.text) }
}
LDS

"$ARM_AS" -mcpu=cortex-m3 -o "$WORK/o.o" "$ASM"
"$ARM_LD" -T "$LDS" -o "$ELF" "$WORK/o.o"

"$QEMU" -M mps2-an385 -kernel "$ELF" -S -gdb tcp::$PORT -display none &
QPID=$!
trap 'kill $QPID 2>/dev/null || true; rm -rf "$WORK"' EXIT
sleep 0.7

# ── Dump QEMU's result/xPSR buffers via gdb ──
OUT="$("$ARM_GDB" -q -batch -nx \
  -ex "target remote :$PORT" -ex 'break done' -ex 'continue' \
  -ex 'printf "DIV   : %08x %08x %08x %08x\n", *(int*)0x2000,*(int*)0x2004,*(int*)0x2008,*(int*)0x200c' \
  -ex 'printf "ALU   : %08x %08x %08x %08x\n", *(int*)0x2010,*(int*)0x2014,*(int*)0x2018,*(int*)0x201c' \
  -ex 'printf "xPSR  : %08x %08x %08x %08x\n", *(int*)0x2020,*(int*)0x2024,*(int*)0x2028,*(int*)0x202c' \
  -ex 'kill' -ex 'quit' "$ELF" 2>&1)" || true

# ── Compare actual vs verified-expected (real exit code) ──
# gdb prints "<LABEL> : w0 w1 w2 w3"; field 1 = label, field 2 = ':', 3..6 = words.
declare -A EXP=(
  [DIV]="00000000 00000000 80000000 00000000"   # SDIV/0=0 both signs; INT_MIN/-1=INT_MIN; udiv/0=0
  [ALU]="00000000 80000000 00000004 fffffffe"   # adc 0xFFFFFFFF+0+C1=0; adc 7FFFFFFF+1=80000000; sbc 5-1=4; sbc 5-7=-2
  [xPSR]="60000000 90000000 20000000 80000000"  # C+Z / N+V / C / N  (top nibble only)
)

fail=0
for label in DIV ALU xPSR; do
  actual="$(printf '%s\n' "$OUT" | awk -v L="$label" '$1==L {print $3,$4,$5,$6}')"
  if [[ -z "$actual" ]]; then
    echo "MISSING $label line in gdb output (gdb/qemu error?)"
    fail=1
  elif [[ "$actual" != "${EXP[$label]}" ]]; then
    echo "MISMATCH $label: got '$actual', expected '${EXP[$label]}'"
    fail=1
  else
    echo "ok       $label: $actual"
  fi
done

if (( fail )); then
  echo "FAIL: QEMU output diverged from expected Tier-1 semantics"
  exit 1
fi
echo "PASS: QEMU Tier-1 reference semantics stable (micro-forge contract holds)"
exit 0
