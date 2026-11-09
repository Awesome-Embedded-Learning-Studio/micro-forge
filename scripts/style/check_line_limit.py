#!/usr/bin/env python3
"""Line-limit gate: no source file may exceed the limit (default 600).

Scans C++ headers/sources and Python scripts under the tracked source
roots. A file over the limit fails the gate -- split it instead of
suppressing.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# micro-forge local: "bench" joined at batch 3 (guest loops + harness are
# tracked sources); the rest stays verbatim from the parent repo.
ROOTS = ("include", "src", "tools", "tests", "scripts", "bench")
# micro-forge local: path prefixes the gate never scans. The emsdk
# toolchain clone under tools/ is gitignored third-party tooling, not
# our sources (batch 5).
SKIPPED_PREFIXES = ("tools/emsdk/",)
EXTS = (".cpp", ".hpp", ".py")
# Inherited verbatim from the parent repo (batch 1): splitting it here would
# fork away from upstream. Revisit if the parent repo splits it.
EXEMPT = {"scripts/doxygen/lint.py"}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=600)
    args = ap.parse_args()

    repo = Path(__file__).resolve().parents[2]
    offenders: list[tuple[int, Path]] = []
    scanned = 0
    for root in ROOTS:
        base = repo / root
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in EXTS or not path.is_file():
                continue
            rel = path.relative_to(repo).as_posix()
            if rel in EXEMPT or rel.startswith(SKIPPED_PREFIXES):
                continue
            scanned += 1
            n = len(path.read_text(encoding="utf-8", errors="replace").splitlines())
            if n > args.limit:
                offenders.append((n, path))
    for n, path in offenders:
        print(f"OVER LIMIT: {n} lines (>{args.limit}): {path}")
    print(f"scanned {scanned} files, limit {args.limit}, "
          f"{len(offenders)} offenders")
    return 1 if offenders else 0


if __name__ == "__main__":
    sys.exit(main())
