#!/usr/bin/env bash
# Fetch a full-day NASDAQ TotalView-ITCH 5.0 sample file into data/ and verify
# it against tools/data-manifest.txt. Resumable; safe to re-run.
#
#   tools/fetch-data.sh [DAY]     DAY defaults to 01302019
#
# The file is ~4.4 GiB and is never committed; .gitignore keeps data/ out.
set -euo pipefail

day="${1:-01302019}"
file="${day}.NASDAQ_ITCH50.gz"
url="https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/${file}"

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
manifest="${repo_root}/tools/data-manifest.txt"
dest_dir="${repo_root}/data"
dest="${dest_dir}/${file}"

expected="$(awk -v f="$file" '$1 == f { print $2 }' "$manifest")"
if [[ -z "$expected" ]]; then
    echo "error: no checksum for ${file} in tools/data-manifest.txt" >&2
    echo "Add a line '<file> <sha256>' after corroborating the digest per the" >&2
    echo "manifest header. Do not invent one." >&2
    exit 1
fi

mkdir -p "$dest_dir"
echo "fetching ${url}"
curl -fSL --retry 4 --retry-delay 2 -C - -o "$dest" "$url"

echo "verifying sha256"
actual="$(sha256sum "$dest" | awk '{ print $1 }')"
if [[ "$actual" != "$expected" ]]; then
    echo "error: checksum mismatch for ${dest}" >&2
    echo "  expected: ${expected}" >&2
    echo "  actual:   ${actual}" >&2
    exit 1
fi

echo "verifying gzip integrity"
gzip -t "$dest"
echo "ok: ${dest}"
