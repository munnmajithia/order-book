# Decision log

Append-only. One entry per engineering decision worth remembering; newest last.

## Scaffold

- **GoogleTest pinned by release-asset tarball URL + SHA256** (not a git tag):
  GitHub guarantees release-asset bytes are immutable, which is what a checksum
  pin wants; no clone per CI job. The checksum matches the widely published
  value for v1.15.2.
- **Warnings**: `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror` from
  commit one. `-Wconversion` is far cheaper to hold from an empty tree than to
  retrofit; revisit only if it fights the parser's byte-swapping code.
- **clang-format**: LLVM base, 4-space indent, 100-column limit, left pointer
  alignment.
- **clang-tidy**: broad check families with warnings-as-errors; each removed
  check carries its reason in `.clang-tidy`. Tests relax
  `readability-function-cognitive-complexity` via `tests/.clang-tidy`.
- **Sanitizer presets kept separate** (asan / ubsan / tsan × both compilers)
  rather than a combined asan+ubsan config: CI cost is trivial and separate
  jobs localize failures.
- **C++ module scanning disabled globally**: no modules planned, and CMake's
  scanner (default-on for C++23 + Ninja + clang) injects `@*.modmap` response
  files that exist only post-build, which breaks configure-only clang-tidy in
  the lint job.

## Parser hardening

- **The fuzz entry point is built by every preset, not just the fuzzing ones.**
  libFuzzer is a clang-only runtime, but `LLVMFuzzerTestOneInput` is ordinary
  code. `ob_fuzz_replay` links it with a plain `main`, so the target compiles
  and lints everywhere, runs under ASan, UBSan and TSan in the PR gate, and a
  crash artifact reproduces from any build rather than only a fuzzer one.
- **The seeded corpus is replayed as a ctest.** Fuzzing itself is a nightly
  job — ten minutes is too slow for a PR gate — but replaying fixture-derived
  inputs through the same entry point costs milliseconds, so the parser's
  hostile-input path is sanitized on every push instead of once a night.
- **The corpus is generated, never committed.** The fixture already holds these
  bytes; a committed corpus would be a second copy free to drift from it.
  `ob_fuzz_seed` writes one seed per message type present in the stream plus
  short multi-frame windows, which starts the fuzzer past the length prefix
  rather than making it rediscover the framing.
- **Two fuzzing presets instead of one combined-sanitizer preset**, keeping the
  scaffold's one-sanitizer-per-preset rule: ASan and UBSan fuzz as separate
  nightly jobs so a failure localizes to one runtime.
- **`-fsanitize=fuzzer-no-link` is applied to the whole fuzz build tree**, not
  just the entry point, so coverage feedback comes from inside the parser.
- **The nightly corpus rolls forward through the Actions cache**: each run
  restores the newest corpus and saves what it grew. A fuzzer restarted from
  seeds every night re-derives the same shallow coverage and never gets deeper.
- **The reader does not advance past a frame it rejected**, which the tests now
  pin: a caller that retries after a `FramingError` sees the same error at the
  same offset instead of a different one further in.
- **Unknown types are accepted at any length the prefix bounds**, including the
  full 65535. The spec fixes no length for them, so the prefix is the only
  authority; rejecting them would make the parser stricter than the protocol and
  break on any future message type.

## Reference book invariants

- **The book being crossed is not a structural invariant.** `check_invariants`
  enforces only what must always hold — the order index agrees with the queues,
  no level is present but empty, every resting order has positive size — because
  those hold on real ITCH data. Best-bid-below-best-ask is exposed separately as
  `is_crossed`: generated flows assert it stays false, but a real stream locks
  and crosses transiently and the structural checks must survive that same data.
- **Index consistency is checked by set comparison, not by dereferencing the
  stored iterators.** Walking the queues into a set of resting refs and comparing
  that to the index catches a count mismatch, a duplicated ref, and an indexed
  order that no longer rests — a stronger check than following each stored list
  iterator, and one the static analyzer can reason about.
- **Invariants run sampled during replay, in full at the end.** The check is
  O(open orders), so running it on all 151,476 fixture messages would be
  quadratic; it samples every 2,000 messages and then runs once on the exact
  final state. The property suite runs it after every generated op, where the
  books are small.
- **Property flows are generated uncrossed on purpose.** Bids are drawn from a
  price band strictly below the ask band, so `is_crossed` staying false is a real
  check on the best-price accessors rather than a tautology, and share
  conservation (added = executed + cancelled + deleted + resting) is checked
  against an independent shadow model.

## Fast book v1

- **Level lookup is an ordered `std::map` per side for v1, not a hash.** The plan
  floated a reserve-sized open-addressing map keyed by (side, price), but v1's
  job is correctness cross-validated against the reference, and an ordered map
  gives the best level as its front and produces the snapshot in the reference's
  order for free. The level-lookup structure is exactly what C3.2 profiles and may
  replace; committing to a hash now would be optimizing without a profile, which
  the project forbids. The distinctive fast mechanics — a slab order pool with a
  free list and intrusive per-level FIFO links — are here from v1.
- **The cross-validation contract is snapshot equality.** Both books emit the same
  text snapshot (same ordering, same FNV-1a trailer), so the dual replay compares
  their full observable state rather than a hand-picked set of fields. It runs
  every 1,000 messages and once exactly at end of fixture; a divergence is a
  stop-the-world bug and fails CI.
- **The fast book tracks level totals incrementally; the reference walks the queue.**
  That is the whole point of having two books — the fast one maintains derived state
  and the slow one recomputes it, so cross-validation catches any drift. A delete
  therefore has to subtract the resting remainder from the level total explicitly,
  where the reference simply stops counting a removed order.
- **C3.1 lands partial (fixture cross-validation only).** Its other two acceptance
  checks are environment-blocked here and unchanged by this work: the sampled
  full-day cross-validation needs the ~4.8 GB day file (egress-blocked), and the
  first ladder entry needs the designated bench machine (this box is not one, so
  every number it would produce is dev-only by construction). Those close on a run
  with the data path or the bench machine available.
