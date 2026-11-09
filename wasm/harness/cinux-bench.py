#!/usr/bin/env python3
"""Compare wasm builds with fresh-process Cinux boots and guest state checks.

taskset -c 2 python3 wasm/harness/cinux-bench.py before.wasm after.wasm \
    --kernel /path/kernel.bin --disk /path/ext2.img --reps 5
"""

import argparse
import json
import os
from pathlib import Path
import statistics
import subprocess


def positive(value):
    """Accept a positive integer CLI argument."""
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be positive")
    return number


def main():
    """Run alternating artifact orders and compare each sample's state."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", nargs="+")
    parser.add_argument("--kernel", default=os.environ.get("MF_KERNEL_BIN"))
    parser.add_argument("--disk", default=os.environ.get("MF_DISK_IMG"))
    parser.add_argument("--instructions", type=positive, default=400_000_000)
    parser.add_argument("--chunk", type=positive, default=2_000_000)
    parser.add_argument("--reps", type=positive, default=5)
    parser.add_argument("--node", default="node")
    args = parser.parse_args()
    if not args.kernel or not args.disk:
        parser.error("--kernel and --disk (or MF_KERNEL_BIN and MF_DISK_IMG) are required")
    harness = Path(__file__).with_suffix(".mjs")
    results = [[] for _ in args.artifacts]
    expected = None
    print(subprocess.check_output([args.node, "--version"], text=True).strip(), flush=True)
    for rep in range(args.reps):
        order = range(len(args.artifacts))
        if rep % 2:
            order = reversed(order)
        for index in order:
            command = [args.node, str(harness), args.artifacts[index],
                       "--kernel", args.kernel, "--disk", args.disk,
                       "--instructions", str(args.instructions), "--chunk", str(args.chunk)]
            sample = json.loads(subprocess.check_output(command, text=True))
            if expected is None:
                expected = sample["state"]
            if sample["state"] != expected:
                raise RuntimeError(f"guest state differs: expected {expected}, got {sample}")
            results[index].append(sample["seconds"])
            print(json.dumps({"rep": rep + 1, **sample}), flush=True)
    medians = [statistics.median(times) for times in results]
    for artifact, seconds in zip(args.artifacts, medians):
        print(f"{artifact}: median {seconds:.3f}s, "
              f"{int(expected['icount']) / seconds / 1e6:.2f} Mips, "
              f"{medians[0] / seconds:.3f}x baseline ({args.reps} reps)")
    print("Guest icount, exit, RIP, serial, console SHA-256 and RAM SHA-256 match.")


if __name__ == "__main__":
    main()
