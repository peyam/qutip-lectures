#!/usr/bin/env python3
"""Regenerate GOLDEN_PAYLOAD (include/golden_payload.hpp) or write it as a raw
binary file usable with `aiw_rx --golden-file`."""
import sys

import numpy as np

payload = np.random.default_rng(42).integers(0, 256, 200, dtype=np.uint8)
if len(sys.argv) > 1:
    payload.tofile(sys.argv[1])
else:
    for i in range(0, 200, 16):
        print("    " + ", ".join(f"0x{b:02x}" for b in payload[i:i + 16]) + ",")
