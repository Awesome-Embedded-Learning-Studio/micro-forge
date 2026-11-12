#!/usr/bin/env python3
"""Bakes jit_templates.S into a generated C++ header.

Why this exists: the JIT templates used to be hand-written byte arrays with
hole offsets counted by eye. Counting was wrong eight separate times and each
mistake cost a debugging round. The assembler knows the offsets, so it owns
them now.

Mechanism: every hole is filled with a unique magic constant; the generator
scans the assembled bytes, records (kind, offset, size), and emits both the
byte array and a hole table. Templates delimited by `tpl_<name>` /
`tpl_<name>_end` symbols.

Usage: scripts/gen/gen_jit_templates.py [--check]
  --check  fail when the checked-in header is stale instead of rewriting it
"""
import argparse
import pathlib
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src/arch/x86_64/jit_templates.S"
OUTPUT = ROOT / "include/micro_forge/base/jit_templates_generated.hpp"

# 32-bit holes. Every value is distinct and none may appear inside a 64-bit
# magic (checked below), otherwise the scan cannot tell them apart.
HOLES32 = {
    0x6B415365: "kBase32",
    0x6B415367: "kDisp32",
    0x6B415369: "kWidth32",
    0x6B41536A: "kSlot32",
    0x6B41536B: "kSrc32",
    0x6B41536C: "kMnem32",
    0x6B41536D: "kCc32",
    0x6B41536E: "kOp32",
    0x6B41536F: "kImm32",
    0x6B415370: "kIdx32",
    0x6B415371: "kShift32",
    0x6B415372: "kDst32",
    0x6B415373: "kAbs32",
    0x6B415379: "kCount32",
    0x6B41537A: "kClsl32",
    0x6B415374: "kRsp32",
    0x6B415375: "kReg32",
    0x6B415376: "kSrcw32",
    0x6B415377: "kZx32",
}
HOLES8 = {0x5A: "kShift8", 0x5B: "kScale8"}
HOLES64 = {
    0x1B4153A5000000C1: "kCpu64",
    0x2B4153A5000000C2: "kHelper64",
    0x3B4153A5000000C3: "kImm64",
    0x4B4153A5000000C4: "kAbs64",
    0x5B4153A5000000C5: "kMyRip64",
    0x6B4153A5000000C6: "kRetAddr64",
    0x7B4153A5000000C7: "kTarget64",
    0x8B4153A5000000C8: "kVaddr64",
    0x9B4153A5000000C9: "kTaken64",
    0xAB4153A5000000CA: "kFall64",
}


def die(message: str) -> None:
    print(f"[gen-jit] error: {message}", file=sys.stderr)
    sys.exit(1)


def check_magics() -> None:
    """A 64-bit magic must not contain any 32-bit magic."""
    four = set(HOLES32)
    for value in HOLES64:
        low = value & 0xFFFFFFFF
        high = value >> 32
        if low in four or high in four:
            die(f"64-bit magic {value:#x} collides with a 32-bit hole")
    if len(set(HOLES32.values())) != len(HOLES32):
        die("duplicate hole names")


def assemble(tmp: pathlib.Path) -> tuple[bytes, list[tuple[str, int]]]:
    obj = tmp / "templates.o"
    binary = tmp / "templates.bin"
    subprocess.run(["as", "-o", str(obj), str(SOURCE)], check=True)
    subprocess.run(
        ["objcopy", "-O", "binary", "--only-section=.text", str(obj), str(binary)],
        check=True,
    )
    text = subprocess.run(
        ["nm", "-n", str(obj)], check=True, capture_output=True, text=True
    ).stdout
    symbols = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)
    names = sorted(n for n in symbols if n.startswith("tpl_") and not n.endswith("_end"))
    return binary.read_bytes(), [(n, symbols[n]) for n in names]


def slice_and_scan(blob: bytes, starts: list[tuple[str, int]]) -> list[dict]:
    out = []
    for index, (name, start) in enumerate(starts):
        end_name = f"{name}_end"
        blob_end = starts[index + 1][1] if index + 1 < len(starts) else len(blob)
        # The end symbol is authoritative; fall back to the next template.
        end = None
        try:
            end = _symbols[end_name]
        except KeyError:
            end = blob_end
        code = blob[start:end]
        holes = scan_holes(code, name)
        out.append({"name": name[4:], "code": code, "holes": holes})
    return out


_symbols: dict[str, int] = {}


def scan_holes(code: bytes, name: str) -> list[dict]:
    holes = []
    taken = [False] * len(code)
    for magic, kind in HOLES64.items():
        needle = struct.pack("<Q", magic)
        pos = code.find(needle)
        while pos != -1:
            holes.append({"kind": kind, "offset": pos, "size": 8})
            for i in range(pos, pos + 8):
                taken[i] = True
            pos = code.find(needle, pos + 8)
    for magic, kind in HOLES32.items():
        needle = struct.pack("<I", magic)
        pos = code.find(needle)
        while pos != -1:
            if not any(taken[pos : pos + 4]):
                holes.append({"kind": kind, "offset": pos, "size": 4})
                for i in range(pos, pos + 4):
                    taken[i] = True
            pos = code.find(needle, pos + 4)
    for magic, kind in HOLES8.items():
        needle = struct.pack("<B", magic)
        pos = code.find(needle)
        hits = 0
        while pos != -1:
            if not taken[pos]:
                holes.append({"kind": kind, "offset": pos, "size": 1})
                taken[pos] = True
                hits += 1
                if hits > 1:
                    die(f"{name}: {kind} appears more than once")
            pos = code.find(needle, pos + 1)
    holes.sort(key=lambda h: h["offset"])
    # Overlap check: two holes must not share a byte.
    for a, b in zip(holes, holes[1:]):
        if a["offset"] + a["size"] > b["offset"]:
            die(f"{name}: holes at {a['offset']} and {b['offset']} overlap")
    return holes


def render(templates: list[dict]) -> str:
    lines = [
        "/// @file jit_templates_generated.hpp",
        "/// @brief GENERATED by scripts/gen/gen_jit_templates.py from",
        "///        src/arch/x86_64/jit_templates.S. Do not edit by hand.",
        "#ifndef MICRO_FORGE_BASE_JIT_TEMPLATES_GENERATED_HPP",
        "#define MICRO_FORGE_BASE_JIT_TEMPLATES_GENERATED_HPP",
        "",
        "#include <array>",
        "#include <micro_forge/base/jit.hpp>",
        "#include <cstddef>",
        "#include <cstdint>"
        "",
        "namespace micro_forge::base::jit_gen {",
        "",
        "/// @brief What a hole is filled with at block-compile time.",
        "enum class HoleKind : std::uint8_t {",
    ]
    kinds = sorted({h["kind"] for t in templates for h in t["holes"]})
    lines += [f"    {k}," for k in kinds]
    lines += [
        "};",
        "",
        "/// @brief One patch site inside a template.",
        "struct Hole {",
        "    HoleKind kind;",
        "    std::uint8_t offset;",
        "    std::uint8_t size;",
        "};",
        "",
        "/// @brief Looks up a hole by kind and renders it as a patch site.",
        "/// @param[in] holes Hole table of the template being compiled.",
        "/// @param[in] kind Which hole to fill.",
        "/// @param[in] value Value written at patch time.",
        "/// @return Patch site; offset/size stay zero when the kind is absent.",
        "template <std::size_t N>",
        "constexpr auto hole_patch(const Hole (&holes)[N], HoleKind kind, std::uint64_t value) {",
        "    for (const auto& hole : holes) {",
        "        if (hole.kind == kind) {",
        "            return JitPage::Patch{.offset = hole.offset, .size = hole.size, .value = value};",
        "        }",
        "    }",
        "    return JitPage::Patch{};",
        "}",
        "",
    ]
    for t in templates:
        # Hole bytes must read as zero in the emitted array: the layout
        # validator rejects a template whose patch sites are pre-filled, and
        # the block compiler writes the real value at patch time.
        body = bytearray(t["code"])
        for hole in t["holes"]:
            for i in range(hole["offset"], hole["offset"] + hole["size"]):
                body[i] = 0
        code = ", ".join(f"0x{b:02X}" for b in body)
        lines.append(f"/// @brief {t['name']} template ({len(t['code'])} bytes).")
        lines.append(
            f"inline constexpr std::array<std::uint8_t, {len(t['code'])}> "
            f"k{t['name'].title().replace('_', '')} = {{{code}}};"
        )
        if t["holes"]:
            table = ", ".join(
                f"{{HoleKind::{h['kind']}, {h['offset']}, {h['size']}}}" for h in t["holes"]
            )
            lines.append(
                f"inline constexpr Hole k{t['name'].title().replace('_', '')}Holes[] = "
                f"{{{table}}};"
            )
        else:
            lines.append(
                f"inline constexpr Hole k{t['name'].title().replace('_', '')}Holes[] = "
                "{HoleKind::kBase32, 0, 0}; // no holes; keeps the array well-formed"
            )
        lines.append("")
    lines += [
        "} // namespace micro_forge::base::jit_gen",
        "",
        "#endif // MICRO_FORGE_BASE_JIT_TEMPLATES_GENERATED_HPP",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--print", action="store_true")
    args = parser.parse_args()
    check_magics()
    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = pathlib.Path(tmpdir)
        blob, starts = assemble(tmp)
        global _symbols
        text = subprocess.run(
            ["nm", "-n", str(tmp / "templates.o")],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        _symbols = {}
        for line in text.splitlines():
            parts = line.split()
            if len(parts) == 3:
                _symbols[parts[2]] = int(parts[0], 16)
    templates = slice_and_scan(blob, starts)
    if not templates:
        die("no tpl_* symbols found")
    header = render(templates)
    if args.print:
        print(header)
    if args.check:
        current = OUTPUT.read_text() if OUTPUT.exists() else ""
        if current != header:
            die(f"{OUTPUT} is stale; rerun without --check")
        print(f"[gen-jit] up to date ({len(templates)} templates)")
        return 0
    OUTPUT.write_text(header)
    total = sum(len(t["code"]) for t in templates)
    print(f"[gen-jit] wrote {len(templates)} templates, {total} bytes -> {OUTPUT.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
