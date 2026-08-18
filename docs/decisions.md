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
