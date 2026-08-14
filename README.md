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

## Results

*(generated from committed benchmark JSON — never hand-typed; pending)*

## Quickstart

*(pending — will be executed verbatim in a clean container as a release gate)*

## License

MIT
