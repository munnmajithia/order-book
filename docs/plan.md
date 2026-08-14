# POC plan — data spine

Goal: prove the path from raw NASDAQ TotalView-ITCH 5.0 bytes to a correct
reference order book, end to end, with every step tested. This baseline covers
the repo scaffold, the measurement harness, the data tooling, the parser
framing/decode layers, and the `std::map` reference book with a golden
end-of-fixture snapshot. Everything later (fast book, matching engine) is
validated against what this POC establishes.

Deliberately out of scope here: parser fuzzing/hardening, property tests beyond
the golden snapshot, the fast book, the matching engine, and any performance
number. Benches exist and run as sanity checks, but no result from a
development machine is publishable (see the operating rules in the README).

## Build order

1. Scaffold: CMake + Ninja presets (gcc/clang × debug/release/asan/ubsan/tsan),
   module layout (`itch/ book/ engine/ queue/ bench/ tools/ demo/`),
   clang-format + clang-tidy enforced, CI on both compilers, MIT.
2. Measurement harness (`bench/`): serialized `rdtscp` cycle clock with
   startup qualification and frequency calibration against
   `CLOCK_MONOTONIC_RAW`; fixed-memory log-bucketed histogram (24 KB, no
   allocation); JSON result files stamped PUBLISHABLE/DEV-ONLY with machine
   config embedded. Unit-tested against exact reference percentiles.
3. Data tooling (`tools/`): checksummed, resumable fetch script for the
   full-day file; deterministic slice tool cutting a ~5 MB CI fixture
   (system/directory prelude + all messages for a fixed symbol set); fixture
   and its message-type census committed.
4. ITCH parser (`itch/`): length-prefixed framing over an mmap'd file; packed
   big-endian message structs for S R A F E C X D U P with sizes
   static-asserted against the spec; zero-copy decode dispatch; census over
   the whole fixture asserted against the committed counts.
5. Reference book (`book/`): per-side `std::map` price levels, FIFO order
   queues, order-id index; applies A F E C X D U P; golden end-of-fixture
   snapshot committed and asserted in CI.

## Commit map

Granular conventional commits, one logical change each, every commit green
(build + tests + lint). Roughly: plan, scaffold, CI, clock, histogram +
tests, bench runner + tests, fetch script, slice tool, fixture + census,
structs + framing + tests, decoder + census test, reference book + golden
snapshot test, README.
