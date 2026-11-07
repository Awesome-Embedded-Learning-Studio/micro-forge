#!/usr/bin/env python3
"""Precise .text profile for the micro-forge executor (batch 3).

The batch-2 profile scanned the whole kernel.bin linearly, so data and
rodaata bytes polluted the histogram. This tool locates the real code
sections via the companion ELF, maps them back into the flat image using
the ImageHeader (load_paddr), and runs the decoder over code bytes only.

The mapping is self-checked: the extracted section bytes must match the
kernel.bin slice byte-for-byte at the computed offset, otherwise the tool
refuses.

Usage:
    text_profile.py <kernel.bin> <kernel ELF> [--dump <mf-decode-dump>]
"""

from __future__ import annotations

import argparse
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

IMAGE_MAGIC = 0x00114514

# Header: magic u32, version u16, header_size u16, load_paddr u64,
# file_size u64, mem_size u64, entry u64 (packed, little-endian).
HEADER = struct.Struct("<IHHQQQQ")


def parse_image_header(blob: bytes) -> dict:
    if len(blob) < HEADER.size:
        raise SystemExit("kernel.bin smaller than an ImageHeader")
    magic, _version, header_size, load_paddr, file_size, _mem, _entry = (
        HEADER.unpack_from(blob, 0))
    if magic != IMAGE_MAGIC:
        raise SystemExit(f"bad ImageHeader magic {magic:#x}")
    return {"header_size": header_size, "load_paddr": load_paddr,
            "file_size": file_size}


def elf_code_sections(elf: Path) -> list[tuple[str, int, int]]:
    """Executable PROGBITS sections (.text* and .init*) as (name, lma, size).

    The kernel links at virtual addresses (0xffffffff80...), but the flat
    image is laid out by load address: the LMA is what maps into
    kernel.bin, so VMA is useless here. Flags come from readelf, LMAs
    from objdump -h; the two are joined by section name.
    """
    flags = subprocess.run(["readelf", "-SW", str(elf)], check=True,
                           capture_output=True, text=True).stdout
    executable = set()
    for line in flags.splitlines():
        m = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+\S+", line)
        if m and "AX" in line:
            executable.add(m.group(1))
    rows = []
    out = subprocess.run(["objdump", "-h", str(elf)], check=True,
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        m = re.match(
            r"\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)",
            line)
        if not m:
            continue
        name, size, _vma, lma = m.group(1), int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16)
        if name not in executable or size == 0:
            continue
        if name.startswith(".text") or name.startswith(".init"):
            rows.append((name, lma, size))
    return rows


def extract(elf: Path, section: str, size: int) -> bytes:
    with tempfile.NamedTemporaryFile(suffix=".bin") as tmp:
        subprocess.run(["objcopy", "-O", "binary", f"--only-section={section}",
                        str(elf), tmp.name], check=True)
        data = Path(tmp.name).read_bytes()
    if len(data) != size:
        raise SystemExit(f"section {section}: objcopy gave {len(data)} bytes, "
                         f"readelf said {size}")
    return data


def stats(dump: str, blob_path: str) -> dict:
    out = subprocess.run([dump, "--stats", blob_path], check=True,
                         capture_output=True, text=True).stdout
    result: dict = {"hist": [], "fails": [], "kinds": {}}
    section = None
    for line in out.splitlines():
        if line.startswith("-- histogram --"):
            section = "hist"
            continue
        if line.startswith("-- first"):
            section = "fails"
            continue
        if section == "hist" and "\t" in line:
            count, name = line.split("\t")
            result["hist"].append((int(count), name))
        elif section == "fails" and line.strip():
            result["fails"].append(int(line, 16))
        elif m := re.match(r"fail_kind_(\d+) (\d+)", line):
            result["kinds"][int(m.group(1))] = int(m.group(2))
        elif m := re.match(r"(decoded_insn_count|fail_site_count) (\d+)", line):
            result[m.group(1)] = int(m.group(2))
    return result


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("kernel_bin")
    ap.add_argument("kernel_elf")
    ap.add_argument("--dump", default="./build/mf-decode-dump")
    args = ap.parse_args()

    blob = Path(args.kernel_bin).read_bytes()
    hdr = parse_image_header(blob)
    sections = elf_code_sections(Path(args.kernel_elf))
    if not sections:
        raise SystemExit("no executable sections found in the ELF")

    print(f"# micro-forge .text profile (batch 3)")
    print(f"# source: {Path(args.kernel_bin).name} ({len(blob)} bytes), "
          f"load_paddr {hdr['load_paddr']:#x}, header {hdr['header_size']:#x}")
    total_decoded = 0
    total_fail = 0
    all_fails: list[int] = []
    all_kinds: dict[int, int] = {}
    merged: dict[str, int] = {}

    for name, lma, size in sections:
        code = extract(Path(args.kernel_elf), name, size)
        # kernel.bin is the flat image of load_paddr itself: the
        # ImageHeader we parsed at offset 0 is the .image_header section
        # (LMA == load_paddr), so no header_size term belongs here.
        off_in_file = lma - hdr["load_paddr"]
        if off_in_file < 0 or off_in_file + size > len(blob):
            raise SystemExit(f"{name}: [{off_in_file:#x}, {off_in_file + size:#x}) "
                             "outside kernel.bin")
        if blob[off_in_file:off_in_file + size] != code:
            raise SystemExit(f"{name}: kernel.bin bytes do not match the ELF section "
                             f"at file offset {off_in_file:#x} (mapping refused)")
        with tempfile.NamedTemporaryFile(suffix=".bin") as tmp:
            Path(tmp.name).write_bytes(code)
            st = stats(args.dump, tmp.name)
        print(f"# {name}: lma {lma:#x} size {size:#x} "
              f"-> file [{off_in_file:#x}, {off_in_file + size:#x}), "
              f"decoded {st.get('decoded_insn_count', 0)}, "
              f"fail sites {st.get('fail_site_count', 0)}")
        total_decoded += st.get("decoded_insn_count", 0)
        total_fail += st.get("fail_site_count", 0)
        all_fails.extend(f + off_in_file for f in st["fails"])
        for k, n in st["kinds"].items():
            all_kinds[k] = all_kinds.get(k, 0) + n
        for count, mn in st["hist"]:
            merged[mn] = merged.get(mn, 0) + count

    print(f"decoded_insn_count {total_decoded}")
    print(f"fail_site_count {total_fail}")
    for k in sorted(all_kinds):
        print(f"fail_kind_{k} {all_kinds[k]}")
    print("-- histogram --")
    for mn, n in sorted(merged.items(), key=lambda kv: -kv[1]):
        print(f"{n}\t{mn}")
    print("-- fail offsets (first 40, kernel.bin file offsets) --")
    for f in sorted(all_fails)[:40]:
        print(f"{f:#x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
