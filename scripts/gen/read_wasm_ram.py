#!/usr/bin/env python3
"""Prints the machine description's [wasm] guest_ram_mb for CMake.

MICRO_FORGE_MACHINE selects machines/<name>.toml (default cinux). Kept as
a script so CMake passes an environment variable instead of interpolating
strings into python source.
"""
import os
import pathlib
import sys
import tomllib

machine = os.environ.get("MICRO_FORGE_MACHINE", "cinux")
path = pathlib.Path(__file__).resolve().parents[2] / "machines" / f"{machine}.toml"
if not path.exists():
    sys.exit(f"unknown machine '{machine}': {path} does not exist")
raw = tomllib.loads(path.read_text())
print(raw["wasm"]["guest_ram_mb"])
