#!/usr/bin/env python3
"""Reject trailing return types (`auto f(...) -> T`) in C++ sources.

Why a script: clang-tidy ships no leading-return check; its only built-in
in this space (modernize-use-trailing-return-type) pushes the opposite
direction and stays disabled in .clang-tidy. This script guards the house
style instead.

Usage:
    python3 scripts/style/check_trailing_return.py [paths...]
Defaults to src include tests. Exit 1 on any hit.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

FUNC_DECL = re.compile(
    r'^\s*(?:template\s*<[^<>]*>\s*)?'
    r'(?:\[\[[a-z]+\]\]\s*)?'
    r'(?:virtual\s+|static\s+|inline\s+|explicit\s+|constexpr\s+|friend\s+)*'
    r'auto\s+\w+\s*\('
)
# A line starting with `->` is a wrapped trailing return type.
ARROW_LINE = re.compile(r'^\s*->')
LOOKBACK = 6  # max lines between the `auto f(` head and its `->` tail


def check(path: Path) -> list[str]:
    hits: list[str] = []
    lines = path.read_text(encoding='utf-8', errors='ignore').splitlines()
    candidates: list[int] = []  # line numbers of `auto f(` heads still open

    for lineno, line in enumerate(lines, start=1):
        if FUNC_DECL.match(line):
            candidates.append(lineno)
            # Same-line form: `auto f(...) -> T`
            if ')' in line and '->' in line[line.rfind(')'):]:
                hits.append(f'{path}:{lineno}: trailing return type: {line.strip()}')
                candidates.remove(lineno)
        elif ARROW_LINE.match(line):
            if candidates and lineno - candidates[-1] <= LOOKBACK:
                hits.append(
                    f'{path}:{candidates[-1]}: trailing return type '
                    f'(arrow at line {lineno}): {lines[lineno - 2].strip()}')
                candidates.clear()
        elif line.strip() and not line.strip().startswith('//'):
            # Any other code line closes stale candidates.
            candidates = [c for c in candidates if lineno - c < LOOKBACK]
    return hits


def main() -> int:
    roots = [Path(a) for a in sys.argv[1:]] or [Path('src'), Path('include'), Path('tests')]
    all_hits: list[str] = []
    for root in roots:
        for path in sorted(root.rglob('*')):
            if path.suffix in ('.h', '.hpp', '.cpp') and path.is_file():
                all_hits.extend(check(path))
    for hit in all_hits:
        print(hit)
    if all_hits:
        print(f'FAILED: {len(all_hits)} trailing return type(s)')
        return 1
    print('No trailing return types')
    return 0


if __name__ == '__main__':
    sys.exit(main())
