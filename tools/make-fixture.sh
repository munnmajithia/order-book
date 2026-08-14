#!/usr/bin/env bash
# Build (or verify) the committed CI fixture from the full day file.
#
#   tools/make-fixture.sh SLICE_BIN            regenerate tests/data/
#   tools/make-fixture.sh SLICE_BIN --check    re-slice and require byte
#                                              identity with the committed
#                                              fixture (the acceptance check)
#
# Fixture identity lives here, not in prose: change the symbols or the ceiling
# and the diff is this file. The slice is cut into scratch first and promoted
# only after the usability assertions pass, so a rejected slice can never sit
# in tests/data/ waiting to be committed by accident.
set -euo pipefail

# What the fixture IS: all messages for these symbols from the start of the
# 2019-01-30 day file, plus the system/directory prelude, under this ceiling.
SYMBOLS="AAPL,MSFT,INTC"
MAX_BYTES=$((5 * 1024 * 1024))
DAY="01302019"

slice_bin="${1:?usage: make-fixture.sh SLICE_BIN [--check]}"
mode="${2:-build}"

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
day_file="${repo_root}/data/${DAY}.NASDAQ_ITCH50.gz"
fixture="${repo_root}/tests/data/fixture.itch"
census="${repo_root}/tests/data/fixture-census.txt"
scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT

if [[ ! -f "$day_file" ]]; then
    echo "error: ${day_file} not found; run tools/fetch-data.sh first" >&2
    exit 1
fi

# pipefail is off for this pipeline on purpose: when the byte ceiling stops
# the slicer early it closes the pipe and gzip exits 141, which is what a
# successful run looks like. The two statuses are judged separately.
set +o pipefail
gzip -dc "$day_file" | "$slice_bin" \
    --symbols "$SYMBOLS" --max-bytes "$MAX_BYTES" \
    --out "${scratch}/fixture.itch" --census "${scratch}/fixture-census.txt"
slice_status=$?
set -o pipefail
if [[ $slice_status -ne 0 ]]; then
    echo "error: slicer failed (exit ${slice_status})" >&2
    exit 1
fi

# Usability assertions: a fixture that cannot exercise the book is a failed
# run with an explanation, not a quiet no-op. Requires a directory entry per
# symbol, session events, adds, removals, and at least one execution.
count() { awk -v t="$1" '$1 == t { print $2 }' "${scratch}/fixture-census.txt"; }
n_syms=$(echo "$SYMBOLS" | tr ',' '\n' | wc -l)
fail=0
[[ "$(count R)" -eq "$n_syms" ]] || { echo "assert: want ${n_syms} R entries, got $(count R)" >&2; fail=1; }
[[ -n "$(count S)" ]] || { echo "assert: no system events" >&2; fail=1; }
adds=$(( $(count A || echo 0) + $(count F || echo 0) ))
[[ "$adds" -gt 0 ]] || { echo "assert: no add orders" >&2; fail=1; }
removals=$(( $(count D || echo 0) + $(count X || echo 0) ))
[[ "$removals" -gt 0 ]] || { echo "assert: no deletes or cancels" >&2; fail=1; }
execs=$(( $(count E || echo 0) + $(count C || echo 0) ))
[[ "$execs" -gt 0 ]] || { echo "assert: no executions" >&2; fail=1; }
if [[ $fail -ne 0 ]]; then
    echo "error: slice failed usability assertions; nothing promoted" >&2
    exit 1
fi

if [[ "$mode" == "--check" ]]; then
    cmp "$fixture" "${scratch}/fixture.itch" || { echo "fixture drifted" >&2; exit 1; }
    cmp "$census" "${scratch}/fixture-census.txt" || { echo "census drifted" >&2; exit 1; }
    echo "ok: committed fixture reproduces byte-identically"
else
    mkdir -p "$(dirname "$fixture")"
    mv "${scratch}/fixture.itch" "$fixture"
    mv "${scratch}/fixture-census.txt" "$census"
    echo "wrote ${fixture} and ${census}"
fi
