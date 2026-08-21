// Full-day cross-validation replay: run an uncompressed ITCH day file through
// the reference book and the fast book together, comparing their snapshots
// every N applied messages and byte-for-byte at end of stream. This is the
// day-scale half of C3.1's cross-validation; the fixture half runs in CI
// (tests/fast_book_xval_test.cpp). Needs the multi-gigabyte day file, so it is
// a local check, not a CI job:
//
//   tools/fetch-data.sh
//   gzip -dk data/01302019.NASDAQ_ITCH50.gz
//   ob_xval_replay data/01302019.NASDAQ_ITCH50 [COMPARE_EVERY]

#include "book/fast_book.hpp"
#include "book/reference_book.hpp"
#include "itch/decoder.hpp"
#include "itch/reader.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <sstream>
#include <string>

namespace {

using namespace ob::itch;
using ob::book::FastBook;
using ob::book::ReferenceBook;

// Snapshots serialize every level of every stock, so at day scale each
// comparison costs real time; 5M applied messages keeps the sample dense
// enough (dozens of checks across the day) without dominating the run.
constexpr uint64_t kDefaultCompareEvery = 5'000'000;

template <typename Book> std::string snapshot(const Book& book) {
    std::ostringstream out;
    book.write_snapshot(out);
    return out.str();
}

struct DualHandler {
    ReferenceBook ref;
    FastBook fast;
    uint64_t compare_every = kDefaultCompareEvery;
    uint64_t applied = 0;
    uint64_t skipped = 0;
    uint64_t unknown = 0;
    uint64_t comparisons = 0;
    bool diverged = false;
    uint64_t diverged_at = 0;

    template <typename Msg> void operator()(const Msg& msg) {
        ref.apply(msg);
        fast.apply(msg);
        if (++applied % compare_every == 0) {
            ++comparisons;
            if (!diverged && snapshot(ref) != snapshot(fast)) {
                diverged = true;
                diverged_at = applied;
            }
            std::cerr << "applied " << applied << " comparisons " << comparisons
                      << (diverged ? " DIVERGED" : " equal") << "\n";
        }
    }
    void on_skipped(const Frame& /*frame*/) { ++skipped; }
    void on_unknown(const Frame& /*frame*/) { ++unknown; }
};

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> args(argv, static_cast<std::size_t>(argc));
    if (args.size() < 2 || args.size() > 3) {
        std::cerr
            << "usage: ob_xval_replay DAY_FILE [COMPARE_EVERY]\n"
            << "DAY_FILE is an uncompressed ITCH stream (tools/fetch-data.sh, then gzip -d)\n";
        return 2;
    }

    DualHandler handler;
    if (args.size() == 3) {
        try {
            handler.compare_every = std::stoull(args[2]);
        } catch (const std::exception&) {
            handler.compare_every = 0;
        }
        if (handler.compare_every == 0) {
            std::cerr << "COMPARE_EVERY must be a positive integer\n";
            return 2;
        }
    }

    try {
        const MappedFile file{std::string(args[1])};
        FrameReader reader(file.bytes());
        while (const auto frame = reader.next()) {
            decode(*frame, handler);
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    const std::string ref_final = snapshot(handler.ref);
    const std::string fast_final = snapshot(handler.fast);
    const bool final_equal = ref_final == fast_final;
    const bool counts_equal = handler.ref.open_order_count() == handler.fast.open_order_count();

    std::cout << "applied            " << handler.applied << "\n"
              << "skipped            " << handler.skipped << "\n"
              << "unknown            " << handler.unknown << "\n"
              << "sampled compares   " << handler.comparisons << "\n"
              << "sampled diverged   "
              << (handler.diverged ? "at message " + std::to_string(handler.diverged_at) : "no")
              << "\n"
              << "final snapshot     " << (final_equal ? "equal" : "DIVERGED") << " ("
              << ref_final.size() << " bytes)\n"
              << "open orders        ref " << handler.ref.open_order_count() << " fast "
              << handler.fast.open_order_count() << "\n";

    const bool clean = !handler.diverged && final_equal && counts_equal;
    std::cout << (clean ? "cross-validation clean" : "cross-validation FAILED") << "\n";
    return clean ? 0 : 1;
}
