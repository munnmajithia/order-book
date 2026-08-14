# order-book

Limit order book + matching engine in C++23, benchmarked against real NASDAQ
TotalView-ITCH 5.0 full-day data.

> 🚧 Early development.

Two artifacts, one core:

1. **Reconstruction** — parse and replay a full trading day of real exchange messages
   through the book. Headline: throughput.
2. **Matching engine** — the same book core matching seeded synthetic order flow under
   price-time priority. Headline: p50/p99/p99.9 latency.

Correctness spine: a deliberately boring `std::map` reference book is the oracle; the
optimized book is cross-validated against it on every replay, and any divergence is treated
as a stop-the-world bug. Every optimization lands as a single commit carrying before/after
benchmark numbers — the full ladder, null results included, ends up in the results table.

## What exists today

The data spine, end to end and tested:

- **Measurement harness** (`bench/`): serialized `rdtscp` cycle clock with startup
  qualification and calibration against `CLOCK_MONOTONIC_RAW`; a fixed-memory
  log-bucketed histogram (24 KB, no allocation, percentile error bounded at 1/128);
  bench results as JSON files stamped `PUBLISHABLE` or `DEV-ONLY` with the machine
  config and disqualifier list embedded.
- **Data tooling** (`tools/`): checksummed resumable fetch of the official full-day
  sample file; a deterministic slice tool that cuts the committed ~5 MB CI fixture
  (system/directory prelude plus every AAPL/MSFT/INTC message under a 5 MiB ceiling);
  `make-fixture.sh --check` re-slices the day file and requires byte identity.
- **ITCH parser** (`itch/`): mmap'd reader over the length-prefixed framing with strict
  validation; packed big-endian structs for the book-relevant types
  (S R A F E C X D U P), sizes static-asserted against the spec; zero-copy decode
  dispatch; a census over the whole fixture asserted exactly against the counts the
  slice tool committed.
- **Reference book** (`book/`): per-side `std::map` price levels, FIFO order queues,
  strict error checking; replays the whole fixture and must reproduce the committed
  golden end-of-fixture snapshot byte for byte in CI.

Deliberately not here yet: parser fuzzing/hardening, property tests beyond the golden
snapshot, the fast book, the matching engine, the SPSC pipeline, and the demo.

## Results

*(generated from committed benchmark JSON — never hand-typed; pending)*

No performance numbers yet, by design: results are published only from a designated
bench machine, and none is designated. The harness enforces this mechanically — every
result file produced anywhere else is stamped `DEV-ONLY` with its reasons listed, and
the table generator refuses to render it.

## Building

Requires CMake ≥ 3.28, Ninja, and GCC 13+ or Clang 18+.

```sh
cmake --preset gcc-release        # or gcc-debug, clang-asan, gcc-tsan, ...
cmake --build --preset gcc-release
ctest --preset gcc-release
```

Presets cover gcc/clang × debug/release/asan/ubsan/tsan. CI runs the full matrix plus
clang-format and clang-tidy; every commit is expected to be green everywhere.

## Working with the data

```sh
tools/fetch-data.sh                                   # ~4.4 GiB into data/, checksummed
tools/make-fixture.sh build/gcc-release/tools/ob_slice --check   # byte-identity check
```

The committed fixture (`tests/data/fixture.itch`, 5,242,875 bytes, 152,005 messages)
carries its message census and golden book snapshot next to it; the parser and book
tests assert against both.

## License

MIT
