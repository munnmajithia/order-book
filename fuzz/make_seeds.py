#!/usr/bin/env python3
"""Slice the CI fixture into libFuzzer seed inputs.

Walks the length-prefixed framing and writes windows of consecutive frames
as individual seed files, so every seed is a well-formed stream the fuzzer
mutates from. Deterministic: the same fixture always yields the same seeds.
"""

import argparse
import sys
from pathlib import Path

FRAMES_PER_SEED = 64
SEED_STRIDE = 2048  # frames between window starts
MAX_SEEDS = 64


def frame_spans(buf: bytes):
    pos = 0
    while pos < len(buf):
        if pos + 2 > len(buf):
            raise SystemExit(f"truncated length prefix at offset {pos}")
        length = int.from_bytes(buf[pos : pos + 2], "big")
        end = pos + 2 + length
        if length < 12 or end > len(buf):
            raise SystemExit(f"fixture is not well-framed at offset {pos}")
        yield pos, end
        pos = end


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("fixture", type=Path, help="framed ITCH stream to slice")
    ap.add_argument("outdir", type=Path, help="directory for seed files")
    args = ap.parse_args()

    buf = args.fixture.read_bytes()
    spans = list(frame_spans(buf))
    args.outdir.mkdir(parents=True, exist_ok=True)

    count = 0
    for i in range(0, len(spans), SEED_STRIDE):
        window = spans[i : i + FRAMES_PER_SEED]
        data = buf[window[0][0] : window[-1][1]]
        (args.outdir / f"seed-{i:07d}.itch").write_bytes(data)
        count += 1
        if count >= MAX_SEEDS:
            break

    print(f"wrote {count} seeds to {args.outdir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
