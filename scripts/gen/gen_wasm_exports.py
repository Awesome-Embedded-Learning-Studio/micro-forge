#!/usr/bin/env python3
"""Derives the wasm export list from the sources.

Usage: scripts/gen/gen_wasm_exports.py [--check]

Scans src/wasm/*.cpp for mf_* function definitions (a name followed by a
parameter list and an opening brace) and writes them, one per line and
sorted, to the output file. The shell recipe prefixes each with '_' and
hands the list to the linker, so renaming or deleting an export updates
the list on the next build instead of silently exporting a dead name
(mf_instance_use outlived its function by exactly this mechanism).
"""
import argparse
import os
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src/wasm"
DEFAULT_OUT = ROOT / "build-wasm/exports.list"
OUTPUT = pathlib.Path(os.environ.get("WASM_EXPORTS_OUT", DEFAULT_OUT))

# A definition: mf_name(params) { — declarations end in ';' and never match.
DEFINITION = re.compile(r"\b(mf_\w+)\s*\([^{};]*\)\s*(?:const\s*)?\{")


def scan() -> list:
    names = set()
    for path in sorted(SOURCE.glob("*.cpp")):
        for m in DEFINITION.finditer(path.read_text()):
            names.add(m.group(1))
    if not names:
        sys.exit("no exports found under src/wasm")
    return sorted(names)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="fail instead of rewriting when stale")
    args = parser.parse_args()

    text = "\n".join(scan()) + "\n"
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != text:
            sys.exit(f"{OUTPUT} is stale; rerun the generator")
        return
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(text)
    print(f"[gen-exports] wrote {len(text.splitlines())} exports -> {OUTPUT}")


if __name__ == "__main__":
    import sys
    main()
