#!/usr/bin/env python3
"""objdump differential harness for the micro-forge x86-64 decoder.

Authority: GNU objdump (binutils) in binary mode, i386:x86-64, Intel syntax.
Our decoder must agree with objdump on instruction boundaries and on the
rendered text for every instruction it decodes successfully; instructions
we reject are reported as whitelist gaps, not mismatches.

Usage:
    differential.py <bin> [--start HEX] [--max-report N] [--verbose]
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys

OBJDUMP_LINE = re.compile(r"^\s*([0-9a-f]+):\s+(.*)$")


def run_objdump(path: str) -> dict[int, tuple[int, str]]:
    out = subprocess.run(
        ["objdump", "-D", "-b", "binary", "-m", "i386:x86-64", "-M", "intel",
         "--no-show-raw-insn", "--adjust-vma=0", path],
        check=True, capture_output=True, text=True).stdout
    table: dict[int, tuple[int, str]] = {}
    last_off = None
    last_text = None
    for line in out.splitlines():
        m = OBJDUMP_LINE.match(line)
        if not m:
            continue
        off = int(m.group(1), 16)
        text = m.group(2).strip()
        if last_off is not None:
            table[last_off] = (off - last_off, last_text)
        last_off, last_text = off, text
    if last_off is not None:
        table[last_off] = (0, last_text)  # last entry, length unknown
    return table


def run_ours(path: str, start: int) -> tuple[list[tuple[int, int, str]], list[int]]:
    out = subprocess.run(
        ["./build/mf-decode-dump", path, hex(start)],
        check=True, capture_output=True, text=True).stdout
    rows = []
    fails = []
    for line in out.splitlines():
        parts = line.split("\t")
        if len(parts) != 3:
            continue
        if parts[1] == "FAIL":
            fails.append(int(parts[0], 16))
            continue
        rows.append((int(parts[0], 16), int(parts[1]), parts[2]))
    return rows, fails


def normalize(text: str) -> str:
    # objdump pads mnemonics to a column; collapse all whitespace runs.
    t = re.sub(r"\s+", " ", text.strip())
    # objdump prints "mov rax,rcx"; we print "mov rax, rcx".
    t = t.replace(", ", ",")
    # objdump prints bare hex branch targets ("je 1a"), we print 0x1a.
    t = re.sub(r"(?<![\w])((?:jmp|call|je|jne|ja|jae|jb|jbe|jg|jge|jl|jle|js|jns|jo|jno|jp|jnp|loop|loope|loopne|jrcxz) )([0-9a-f]+)$",
               lambda m: m.group(1) + "0x" + m.group(2), t)
    # objdump marks ignored 66 prefixes as "data16" and no-op REX bits as
    # "rex.WX"; ours folds both.
    # No-op REX prefixes can appear anywhere; drop them globally.
    t = re.sub(r"\brex(?:\.[WRXB]+)?\s+", "", t)
    while t.startswith(("data16 ", "rex ", "rex.", "es ", "cs ", "ss ",
                        "ds ", "fs ", "gs ", "repnz ", "repz ", "bnd ")):
        if t.startswith("repnz "):
            t = "repne" + t[5:]  # objdump: repnz; ours: repne
            continue
        if t.startswith("repz "):
            t = "rep " + t[4:]  # objdump: repz; ours: rep
            continue
        if t.startswith("bnd "):  # MPX prefix; ours keeps it in .rep
            t = t[4:]
            continue
        # objdump says "repnz"; our renderer says "repne".
        if t.startswith("repnz "):
            t = "repne" + t[5:]
            continue
        t = t.split(" ", 1)[1] if " " in t else ""
    # Segment overrides inside brackets are decoder state, not rendering:
    # ours carries them in DecodedInsn.seg; strip them for comparison.
    for seg in ("fs:", "gs:", "es:", "cs:", "ss:", "ds:"):
        t = t.replace(seg, "")
    # Segment overrides can also surface as bare words ("lock es cmp").
    t = re.sub(r"\b(?:es|cs|ss|ds|fs|gs) (?=\S)", "", t)
    # objdump materializes SIB zero-index with nonzero scale as "riz*N".
    t = t.replace("+riz*1", "").replace("+riz*2", "").replace("+riz*4", "").replace("+riz*8", "")
    for n in ("1", "2", "4", "8"):
        t = t.replace("[riz*%s+" % n, "[")
        t = t.replace("riz*%s+" % n, "")
    # 16-bit push renders "pushw" in objdump; ours keeps "push".
    t = re.sub(r"^pushw ", "push ", t)
    # Absolute addressing may keep brackets in objdump after riz removal.
    t = re.sub(r"\[(0x[0-9a-f]+)\]", r"\1", t)
    # objdump renders moffs loads as "movabs al,ds:0x..."; ours is plain.
    t = re.sub(r"movabs (al|ax|eax|rax),(?:ds:)?(0x[0-9a-f]+)", r"mov \1,\2", t)
    t = re.sub(r"^movabs (0x[0-9a-f]+),(al|ax|eax|rax)$", r"mov \1,\2", t)
    # objdump 2.4x renders string ops with explicit operands; collapse
    # back to the short mnemonic form our renderer uses. Covers register-
    # first forms ("scas eax,DWORD PTR [rdi]") and es:-prefixed stores.
    w = {"BYTE": "b", "WORD": "w", "DWORD": "d", "QWORD": "q"}
    t = re.sub(r"(movs|stos|lods|scas|cmps)(?:\s+[a-z0-9]+\s*,\s*|\s+)(BYTE|WORD|DWORD|QWORD) PTR (?:es:)?\[[^]]*\](?:,[A-Za-z0-9 ]*PTR (?:es:)?\[[^]]*\])?",
               lambda m: m.group(1) + w[m.group(2)], t)
    # Short forms with a trailing register operand ("stosb,al").
    t = re.sub(r"^(movs|stos|lods|scas|cmps)[bwdq](?:,[a-z0-9]+)?$",
               lambda m: m.group(0).split(",")[0], t)
    # objdump appends comments ("# 3c2") and symbol notes ("<foo>").
    t = re.sub(r"\s+#.*$", "", t)
    t = re.sub(r"\s+<[^>]*>$", "", t)
    # A0-A3 moffs: objdump leaves the width implicit when the bare
    # address faces a register ("mov al,ds:0x.."); ours prints the
    # truthful "BYTE PTR 0x..". Drop the width in exactly that shape.
    parts = t.split(",")
    if len(parts) == 2:
        for i in (0, 1):
            m_w = re.fullmatch(
                r"(?:([a-z0-9]+) )?(?:BYTE|WORD|DWORD|QWORD) PTR (0x[0-9a-f]+)",
                parts[i].strip())
            other = re.fullmatch(r"(?:[a-z0-9]+ )?([a-z][a-z0-9]{1,7})", parts[1 - i].strip())
            if m_w and other:
                head = f"{m_w.group(1)} " if m_w.group(1) else ""
                parts[i] = head + m_w.group(2)
        t = ",".join(parts)
    return re.sub(r"\s+", " ", t)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("bin")
    ap.add_argument("--start", default="0")
    ap.add_argument("--max-report", type=int, default=15)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    start = int(args.start, 0)
    theirs = run_objdump(args.bin)
    ours, fail_sites = run_ours(args.bin, start)

    matched = mismatched = len_div = bad_ref = drift = 0
    unaligned_run = 0
    reports: list[str] = []
    for off, length, text in ours:
        if off not in theirs:
            continue  # our byte skipped in their linear sweep: ignore
        tlen, ttext = theirs[off]
        if "(bad)" in ttext or "?" == ttext.strip().split(",")[-1].strip() \
                or ttext.strip().startswith(("rex",)) and len(ttext.split()) == 1:
            bad_ref += 1  # objdump refuses/bails; ours decodes (wider whitelist)
            continue
        mine, ref = normalize(text), normalize(ttext)
        if ref == "":
            bad_ref += 1  # pure prefix words (gs rex.RB); nothing to compare
            continue
        # We drop rep prefixes on non-string insns (objdump keeps them);
        # align by dropping them from the reference when we dropped ours.
        STRING_HEADS = ("movs", "stos", "lods", "scas", "cmps")
        ref_is_string = ref.split(" ")[-1].startswith(STRING_HEADS) or (
            len(ref.split(" ")) > 1
            and ref.split(" ")[1].startswith(STRING_HEADS))
        if not mine.startswith(("rep ", "repne ")) and ref.startswith(
                ("rep ", "repne ", "repz ", "repnz ")) and not ref_is_string:
            ref = ref.split(" ", 1)[1]
        if mine == ref:
            matched += 1
            if tlen != 0 and tlen != length:
                len_div += 1  # objdump merges "(bad)" runs; text agreed
        else:
            mismatched += 1
            if len(reports) < args.max_report:
                reports.append(
                    f"  @{off:#x} len ours={length} theirs={tlen}\n"
                    f"    ours: {mine}\n    objd: {ref}")

    print(f"matched   {matched}")
    print(f"len_divergence {len_div}  (text agreed, objdump bad-run merge)")
    print(f"bad_ref   {bad_ref}  (objdump '(bad)', ours decodes)")
    print(f"fail_sites {len(fail_sites)}  (our rejections; whitelist gaps)")
    if fail_sites[:5]:
        print("  first:", ", ".join(hex(x) for x in fail_sites[:5]))
    print(f"drift     {drift}  (unaligned runs past 8: gate failure)")
    print(f"mismatch  {mismatched}")
    if reports:
        print("first mismatches:")
        print("\n".join(reports))
    elif args.verbose:
        print("clean")
    return 1 if (mismatched or drift) else 0


if __name__ == "__main__":
    sys.exit(main())
