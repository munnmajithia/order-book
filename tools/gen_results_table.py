#!/usr/bin/env python3
"""Generate the README results table from committed bench JSON files.

Refuses any result file not stamped PUBLISHABLE. --allow-dev renders dev-only
results with a banner attached; that mode exists for local inspection and its
output must never be committed to the README.
"""

import argparse
import json
import sys
from pathlib import Path


def load(path: Path) -> dict:
    with path.open() as f:
        return json.load(f)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("results", nargs="+", type=Path, help="bench JSON result files")
    ap.add_argument("--allow-dev", action="store_true",
                    help="render DEV-ONLY results with a banner (never commit this output)")
    args = ap.parse_args()

    rows = []
    for path in args.results:
        r = load(path)
        stamp = r.get("stamp", "DEV-ONLY")
        if stamp != "PUBLISHABLE" and not args.allow_dev:
            print(f"refusing {path}: stamped {stamp} "
                  f"(disqualifiers: {', '.join(r.get('disqualifiers', [])) or 'none listed'})",
                  file=sys.stderr)
            return 1
        rows.append((r, stamp))

    if any(stamp != "PUBLISHABLE" for _, stamp in rows):
        print("> **DEV-ONLY results** — not from the designated machine; "
              "numbers below are not comparable or publishable.\n")

    print("| bench | p50 | p99 | p99.9 | runs | machine |")
    print("|---|---|---|---|---|---|")
    for r, stamp in rows:
        runs = r.get("runs", [])
        med = sorted(x["p50_ns"] for x in runs)[len(runs) // 2] if runs else 0
        p99 = max((x["p99_ns"] for x in runs), default=0)
        p999 = max((x["p999_ns"] for x in runs), default=0)
        machine = r.get("machine", {}).get("cpu_model", "unknown")
        name = r.get("name", "?")
        tag = "" if stamp == "PUBLISHABLE" else " (DEV-ONLY)"
        print(f"| {name}{tag} | {med} ns | {p99} ns | {p999} ns | {len(runs)} | {machine} |")
    return 0


if __name__ == "__main__":
    sys.exit(main())
