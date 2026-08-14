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
