#!/usr/bin/env python3
"""Compatibility entry point for the pinned raise2-side-patches submodule."""
from pathlib import Path
import runpy

implementation = Path(__file__).resolve().parents[1] / 'side-patches/tools/patch-side-led-maps.py'
if not implementation.is_file():
    raise RuntimeError('Missing side-patches submodule: run git submodule update --init --recursive')
exports = runpy.run_path(str(implementation))
for name in ('patch', 'validate', 'STOCK_SHA256', 'PATCHED_SHA256', 'STOCK_URL', 'IMAGE_BIAS', 'PATCHES'):
    globals()[name] = exports[name]

if __name__ == '__main__':
    exports['main']()
